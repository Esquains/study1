import dataclasses
import itertools
import sympy
from sympy.logic.boolalg import BooleanAtom, Boolean as SympyBoolean
import operator
import math
import logging
import torch
from typing import Union, Dict, Optional, SupportsFloat

from torch._prims_common import dtype_to_type
from .interp import sympy_interp

log = logging.getLogger(__name__)

__all__ = ["ValueRanges", "ValueRangeAnalysis", "bound_sympy"]

class ValueRangeError(RuntimeError):
    pass


# Like sympify, but supports less stuff, and also ensures that direct
# sympy expressions don't have free variables
def simple_sympify(e):
    if isinstance(e, bool):
        return sympy.true if e else sympy.false
    elif isinstance(e, int):
        return sympy.Integer(e)
    elif isinstance(e, float):
        # infinity is special; we use it to bracket integers as well
        if math.isinf(e):
            return sympy.oo if e > 0 else -sympy.oo
        return sympy.Float(e)
    elif isinstance(e, sympy.Expr):
        assert e.is_constant(), e
        # NaNs can occur when doing things like 0 * sympy.oo, but it is better
        # if the operator notices this and takes care of it, because sometimes
        # the NaN is inappropriate (for example, for ints, the [-oo, oo] range
        # should go to zero when multiplied with [0, 0])
        assert e != sympy.nan
        return e
    elif isinstance(e, BooleanAtom):
        return e
    else:
        raise AssertionError(f"not simple sympy type {type(e)}: {e}")


# Sympy atomics only. Unlike <=, it also works on Sympy bools.
def sympy_generic_le(lower, upper):
    if isinstance(lower, sympy.Expr):
        assert isinstance(upper, sympy.Expr)
        return lower <= upper
    else:
        # only negative condition is True > False
        assert isinstance(lower, SympyBoolean) and isinstance(upper, SympyBoolean)
        return not (lower and not upper)


@dataclasses.dataclass(frozen=True)
class ValueRanges:
    # Although the type signature here suggests you can pass any
    # sympy expression, in practice the analysis here only works
    # with constant sympy expressions
    lower: Union[sympy.Expr, SympyBoolean]
    upper: Union[sympy.Expr, SympyBoolean]
    is_bool: bool

    def __init__(self, lower, upper):
        lower = simple_sympify(lower)
        upper = simple_sympify(upper)
        # TODO: when the bounds have free variables, this may be
        # nontrivial to actually verify
        if not sympy_generic_le(lower, upper):
            raise ValueRangeError(f"Invalid ranges [{lower}:{upper}]")
        # Because this is a frozen class
        object.__setattr__(self, "lower", lower)
        object.__setattr__(self, "upper", upper)
        object.__setattr__(self, "is_bool", isinstance(lower, SympyBoolean))
        assert isinstance(upper, SympyBoolean) == self.is_bool

    def __contains__(self, x):
        x = simple_sympify(x)
        return sympy_generic_le(self.lower, x) and sympy_generic_le(x, self.upper)

    def tighten(self, other) -> "ValueRanges":
        """Given two ValueRanges, returns their intersection"""
        return self & other

    # Intersection
    def __and__(self, other) -> "ValueRanges":
        if other == ValueRanges.unknown():
            return self
        if self == ValueRanges.unknown():
            return other
        assert self.is_bool == other.is_bool, (self, other)
        if self.is_bool:
            range = ValueRanges(sympy.Or(self.lower, other.lower), sympy.And(self.upper, other.upper))
        else:
            range = ValueRanges(sympy.Max(self.lower, other.lower), sympy.Min(self.upper, other.upper))
        return range

    # Union
    def __or__(self, other) -> "ValueRanges":
        if ValueRanges.unknown() in (self, other):
            return ValueRanges.unknown()
        assert self.is_bool == other.is_bool, (self, other)
        if self.is_bool:
            range = ValueRanges(sympy.And(self.lower, other.lower), sympy.Or(self.upper, other.upper))
        else:
            range = ValueRanges(sympy.Min(self.lower, other.lower), sympy.Max(self.upper, other.upper))
        return range

    def is_singleton(self) -> bool:
        return self.lower == self.upper

    # TODO: this doesn't work with bools but arguably it should
    @classmethod
    def unknown(cls):
        return cls(-sympy.oo, sympy.oo)

    @classmethod
    def wrap(cls, arg):
        if isinstance(arg, ValueRanges):
            return arg
        return ValueRanges(arg, arg)

    @classmethod
    def increasing_map(cls, x, fn):
        """Increasing: x <= y => f(x) <= f(y)"""
        x = cls.wrap(x)
        return ValueRanges(fn(x.lower), fn(x.upper))

    @classmethod
    def decreasing_map(cls, x, fn):
        """Decreasing: x <= y => f(x) >= f(y)"""
        x = cls.wrap(x)
        return ValueRanges(fn(x.upper), fn(x.lower))

    @classmethod
    def monotone_map(cls, x, fn):
        """It's increasing or decreasing"""
        x = cls.wrap(x)
        l = fn(x.lower)
        u = fn(x.upper)
        return ValueRanges(min(l, u), max(l, u))

    @classmethod
    def convex_min_zero_map(cls, x, fn):
        """fn is convex and has a minimum at 0"""
        x = ValueRanges.wrap(x)
        if 0 in x:
            return ValueRanges(0, max(fn(x.lower), fn(x.upper)))
        else:
            return cls.monotone_map(x, fn)

    @classmethod
    def coordinatewise_increasing_map(cls, x, y, fn):
        """
        Increasing on each coordinate. Mathematically:
        For every 1 <= i <= n and x_i <= y_i we have that
        f(x1, .., xn) <= f(x1, , yi, ..., xn)
        """
        x, y = cls.wrap(x), cls.wrap(y)
        return ValueRanges(
            fn(x.lower, y.lower),
            fn(x.upper, y.upper),
        )

    @classmethod
    def coordinatewise_monotone_map(cls, x, y, fn):
        """It's increasing or decreasing on each coordinate"""
        x, y = cls.wrap(x), cls.wrap(y)
        products = [
            fn(a, b)
            for a, b in itertools.product([x.lower, x.upper], [y.lower, y.upper])
        ]
        return ValueRanges(min(products), max(products))

class SymPyValueRangeAnalysis:
    """
    It gives bounds on a SymPy operator given bounds on its arguments
    See the function `bound_sympy` for a function that applies this logic to a full SymPy expression
    """

    @staticmethod
    def constant(value, dtype):
        # NB: value is NOT a sympy expression, it's a constant!
        is_python = isinstance(value, (int, float, bool))
        assert is_python or isinstance(value, (BooleanAtom, sympy.Integer, sympy.Number))

        # using nan makes subsequent computation throw, and for the purposes of optimization
        # returning -math.inf - math.inf is equivalent to giving up
        if isinstance(value, SupportsFloat) and math.isnan(value):
            return ValueRanges.unknown()

        if is_python:
            type_ = dtype_to_type(dtype)
            value = type_(value)
        else:
            # We do a type check on a best-effort basis
            # We don't want to force a cast to sympy.Float if the value is Rational to avoid losing precision
            if dtype == torch.bool:
                assert isinstance(value, BooleanAtom)
            elif dtype.is_floating_point:
                assert not value.is_finite or value.is_real
            else:
                # dtype is intXX
                assert value.is_integer

        return ValueRanges.wrap(value)

    @staticmethod
    def not_(a):
        a = ValueRanges.wrap(a)
        assert a.is_bool
        return ValueRanges.decreasing_map(a, sympy.Not)

    @staticmethod
    def or_(a, b):
        return ValueRanges.coordinatewise_increasing_map(a, b, sympy.Or)

    @staticmethod
    def and_(a, b):
        return ValueRanges.coordinatewise_increasing_map(a, b, sympy.And)

    @staticmethod
    def eq(a, b):
        a = ValueRanges.wrap(a)
        b = ValueRanges.wrap(b)
        if a.is_singleton() and b.is_singleton() and a.lower == b.lower:
            return ValueRanges.wrap(sympy.true)
        elif a.lower > b.upper or b.lower > a.upper:  # ranges disjoint
            return ValueRanges.wrap(sympy.false)
        return ValueRanges(sympy.false, sympy.true)

    @classmethod
    def ne(cls, a, b):
        return cls.not_(cls.eq(a, b))

    @classmethod
    def lt(cls, a, b):
        a = ValueRanges.wrap(a)
        b = ValueRanges.wrap(b)
        assert a.is_bool == b.is_bool
        if a.is_bool:
            return cls.and_(cls.not_(a), b)
        else:
            if a.upper < b.lower:
                return ValueRanges.wrap(sympy.true)
            elif a.lower >= b.upper:
                return ValueRanges.wrap(sympy.false)
            return ValueRanges(sympy.false, sympy.true)

    @classmethod
    def gt(cls, a, b):
        return cls.lt(b, a)

    @classmethod
    def le(cls, a, b):
        return cls.not_(cls.gt(a, b))

    @classmethod
    def ge(cls, a, b):
        return cls.not_(cls.lt(a, b))

    @staticmethod
    def add(a, b):
        return ValueRanges.coordinatewise_increasing_map(a, b, operator.add)

    @classmethod
    def mul(cls, a, b):
        a = ValueRanges.wrap(a)
        b = ValueRanges.wrap(b)

        assert a.is_bool == b.is_bool
        if a.is_bool:
            return cls.and_(a, b)

        def safe_mul(a, b):
            # Make unknown() * wrap(0) == wrap(0)
            if a == 0:
                return a
            elif b == 0:
                return b
            else:
                return a * b

        return ValueRanges.coordinatewise_monotone_map(a, b, safe_mul)

    @classmethod
    def div(cls, a, b):
        return cls.truediv(a, b)

    @staticmethod
    def truediv(a, b):
        a = ValueRanges.wrap(a)
        b = ValueRanges.wrap(b)
        if 0 in b or ((-sympy.oo in a or sympy.oo in a) and (-sympy.oo in b or sympy.oo in b)):
            return ValueRanges.unknown()
        else:
            return ValueRanges.coordinatewise_monotone_map(a, b, operator.truediv)

    @staticmethod
    def floordiv(a, b):
        a = ValueRanges.wrap(a)
        b = ValueRanges.wrap(b)
        if 0 in b or ((-sympy.oo in a or sympy.oo in a) and (-sympy.oo in b or sympy.oo in b)):
            return ValueRanges.unknown()
        else:
            return ValueRanges.coordinatewise_monotone_map(a, b, operator.floordiv)

    @staticmethod
    def mod(x, y):
        x = ValueRanges.wrap(x)
        y = ValueRanges.wrap(y)
        if x.is_singleton() and y.is_singleton() and y.lower != 0:
            return ValueRanges.wrap(x.lower % y.lower)
        if y.lower <= 0:
            return ValueRanges.unknown()
        return ValueRanges(0, y.upper)

    @classmethod
    def modular_indexing(cls, a, b, c):
        return cls.mod(cls.floordiv(a, b), c)

    @classmethod
    def pow(cls, a, b):
        def is_integer(val):
            return isinstance(val, int) or (
                hasattr(val, "is_integer") and val.is_integer
            )

        a = ValueRanges.wrap(a)
        b = ValueRanges.wrap(b)
        # Not implemented yet. It's a bit tricky
        # If you want to implement it, compute the partial derivatives of a ** b
        # and check the ranges where the function is increasing / decreasing
        # Another non-tight way of doing this is defaulting to doing noting that for a > 0,  a ** b == exp(b * log(a))
        # If this second option is implemented, by carefult about the types and possible infinities here and there.
        if not b.is_singleton():
            return ValueRanges.unknown()

        b = b.lower
        if a.is_singleton():
            a = a.lower
            r = a ** b
            if not r.is_finite:
                return ValueRanges.unknown()
            return ValueRanges.wrap(r)

        if b == 0:
            if not a.lower.is_finite:
                return ValueRanges.unknown()
            type_ = sympy.Float if a.lower.is_real else sympy.Integer
            return ValueRanges.wrap(type_(1))

        if b < 0:
            a = cls.reciprocal(a)
            b = -b

        if a == ValueRanges.unknown():
            return ValueRanges.unknown()

        # Here b > 0
        if not is_integer(b):
            # If the base is positive, then we're good, otherwise nothing's defined
            if a.lower >= 0:
                return ValueRanges.increasing_map(a, lambda x: x ** b)
            else:
                return ValueRanges.unknown()
        else:
            # b > 0 integer
            if b % 2 == 0:
                # x^n where n is even
                return ValueRanges.convex_min_zero_map(a, lambda x: x ** b)
            else:
                # x^n where n is odd
                return ValueRanges.increasing_map(a, lambda x: x ** b)

    @staticmethod
    def reciprocal(x):
        """ Needed as it's used in pow, but it won't appear on a SymPy expression """
        x = ValueRanges.wrap(x)
        if 0 in x:
            return ValueRanges.unknown()
        else:
            return ValueRanges.decreasing_map(x, lambda y: 1 / y)

    @staticmethod
    def abs(x):
        return ValueRanges.convex_min_zero_map(x, abs)

    @staticmethod
    def exp(x):
        return ValueRanges.increasing_map(x, sympy.functions.elementary.exponential.exp)

    @staticmethod
    def log(x):
        x = ValueRanges.wrap(x)
        if x.lower <= 0:
            return ValueRanges.unknown()
        return ValueRanges.increasing_map(x, sympy.log)

    @classmethod
    def minimum(cls, a, b):
        return cls.min_or_max(a, b, sympy.Min)

    @classmethod
    def maximum(cls, a, b):
        return cls.min_or_max(a, b, sympy.Max)

    @staticmethod
    def min_or_max(a, b, fn):
        a = ValueRanges.wrap(a)
        b = ValueRanges.wrap(b)

        # Performs upcasting first
        def fn_(x, y):
            # Poorman's version of upcasting in Sympy
            # Inf is not a float...
            if x.is_Integer and y.is_Integer:
                result_type = sympy.Integer
            elif x.is_rational and y.is_rational:
                result_type = sympy.Rational
            else:
                assert x.is_real or not x.is_finite or y.is_real or not y.is_finite
                result_type = sympy.Float
            return fn(result_type(x), result_type(y))

        return ValueRanges.coordinatewise_increasing_map(a, b, fn_)

    @classmethod
    def floor(cls, x):
        return ValueRanges.increasing_map(x, sympy.functions.elementary.integers.floor)

    @classmethod
    def ceil(cls, x):
        return ValueRanges.increasing_map(x, sympy.functions.elementary.integers.ceiling)

    # It's used in some models on symints
    @staticmethod
    def sqrt(x):
        x = ValueRanges.wrap(x)
        if x.lower < 0:
            return ValueRanges.unknown()
        return ValueRanges.increasing_map(x, sympy.sqrt)

    @staticmethod
    def where(a, b, c):
        b = ValueRanges.wrap(b)
        c = ValueRanges.wrap(c)
        assert a.is_bool
        assert b.is_bool == c.is_bool
        if b.is_bool:
            return ValueRanges(sympy.And(b.lower, c.lower), sympy.Or(b.upper, c.upper))
        else:
            return ValueRanges(sympy.Min(b.lower, c.lower), sympy.Max(b.upper, c.upper))

    # expr_cond_pair is used to represent a single (expr, condition) pair in piecewise.
    # We just return the value range of the expression and its corresponding condition as a tuple
    # and defer the analysis to piecewise
    @staticmethod
    def expr_cond_pair(a, b):
        assert b.is_bool, f"expect cond_expr's ValueRange to be a boolean range but got {b}"
        return (a, b)

    # piecewise function can be used to convert a SymBool to SymInt:
    # int_expr = Piecewise((1, bool_expr), (0, True)), it evalutes to 1 when sym_bool is True and 0 otherwise.
    #
    # ranges is a sequence of (expr_range, condition_range) pairs. The range pair is constructed in expr_cond_pair.
    # The ValueRange of Piecewise is just the union of all expr ranges whose condition expr can be True.
    @staticmethod
    def piecewise(*ranges):
        init_range = None
        for expr_range, cond_range in ranges:
            if sympy.true in cond_range:
                if init_range is None:
                    init_range = expr_range
                else:
                    init_range = init_range | expr_range
        return init_range


class ValueRangeAnalysis(SymPyValueRangeAnalysis):
    def __init__(self):
        self.name = "ValueRangeAnalysis"
        boolean_operators = (
            "xor",
            "logical_and",
            "logical_or",
            "logical_not",
        )
        for op in boolean_operators:
            setattr(self, op, self.bool_handler)

    @staticmethod
    def bool_handler(*args, **kwargs):
        # just assuming bools can have both values
        return ValueRanges(sympy.false, sympy.true)  # type: ignore[arg-type]

    @staticmethod
    def default_handler(*args, **kwargs):
        # many ops are unlikely to show up in optimizable indexing compute,
        # so we dont have full coverage
        return ValueRanges.unknown()

    def load(self, name: str, index: sympy.Expr):
        return ValueRanges.unknown()

    def store(self, name, index, value, mode=None):
        return

    def reduction(self, name, dtype, src_dtype, reduction_type, index, value):
        return ValueRanges.unknown()

    def index_expr(self, index, dtype):
        assert isinstance(index, ValueRanges)
        return index

    @staticmethod
    def to_dtype(x, dtype: torch.dtype):
        x = ValueRanges.wrap(x)

        if dtype == torch.bool:
            if x.is_singleton():
                return ValueRanges.wrap(x.lower != 0)
            elif 0 not in x:
                return ValueRanges.wrap(sympy.true)
            else:
                return ValueRanges(sympy.false, sympy.true)

        def cast(x, dtype):
            # dtype is int or float
            if dtype.is_floating_point:
                return sympy.Float(x)
            else:
                try:
                    return sympy.Integer(x)
                except TypeError:
                    # inf cannot be cast to Integer
                    return x

        if x.is_bool:
            if x.is_singleton():
                val = 1 if x.lower else 0
                return ValueRanges.wrap(cast(val, dtype))
            else:
                return ValueRanges(cast(0, dtype), cast(1, dtype))
        else:
            # int to float or float to int
            return ValueRanges(cast(x.lower, dtype), cast(x.upper, dtype))

    @staticmethod
    def square(x):
        return ValueRanges.convex_min_zero_map(x, lambda y: y * y)

    @staticmethod
    def neg(x):
        return ValueRanges.decreasing_map(x, operator.neg)

    @classmethod
    def truncdiv(cls, a, b):
        x = cls.truediv(a, b)
        if x == ValueRanges.unknown():
            return x

        def trunc(x):
            return sympy.Integer(x) if x.is_finite else x

        return ValueRanges.increasing_map(x, trunc)

    @classmethod
    def sub(cls, a, b):
        return cls.add(a, cls.neg(b))

    def __getattr__(self, name):
        log.debug("unhandled ValueRange op %s", name)
        return self.default_handler


def bound_sympy(expr: sympy.Expr, ranges: Optional[Dict[sympy.Symbol, ValueRanges]] = None) -> ValueRanges:
    if isinstance(expr, sympy.Number):
        return ValueRanges.wrap(expr)

    ranges = ranges or {}

    # If there's a tracing context, augment available constrained ranges.
    context = torch._guards.TracingContext.get()
    if context and context.fake_mode.shape_env:
        ranges = {**ranges, **context.fake_mode.shape_env.var_to_range}

    unbounded_vars = expr.free_symbols - ranges.keys()
    if unbounded_vars:
        # Give some bounds to the free variables via their SymPy assumptions
        # TODO A better way of doing this would be to assign them a range upon creation, as
        #      size variables can come with a lower bound of 2, as we specialise on 0 and 1
        unbounded_ranges: Dict[sympy.Symbol, ValueRanges] = {}
        for s in unbounded_vars:
            assert s.is_integer  # type: ignore[attr-defined]
            if s.is_positive:  # type: ignore[attr-defined]
                lower = 1
            elif s.is_nonnegative:  # type: ignore[attr-defined]
                lower = 0
            else:
                lower = -math.inf  # type: ignore[assignment]
            unbounded_ranges[s] = ValueRanges(lower, math.inf)  # type: ignore[index]
        ranges = {**ranges, **unbounded_ranges}

    return sympy_interp(SymPyValueRangeAnalysis, ranges, expr)
