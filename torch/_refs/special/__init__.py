import math
from typing import Optional, Union

import torch
import torch._prims as prims
import torch._prims_common as utils
import torch._refs as refs

from torch import Tensor
from torch._decomp import register_decomposition
from torch._prims_common import (
    ELEMENTWISE_TYPE_PROMOTION_KIND,
    Number,
    NumberType,
    TensorLike,
    TensorLikeType,
)
from torch._prims_common.wrappers import elementwise_type_promotion_wrapper, out_wrapper
from torch._refs import (
    _make_elementwise_binary_reference,
    _make_elementwise_unary_reference,
)


__all__ = [
    "bessel_j0",
    "bessel_j1",
    "entr",
    "erfcx",
    "expit",
    "i0e",
    "i1",
    "i1e",
    "log_ndtr",
    "logit",
    "xlog1py",
    "multigammaln",
    "ndtr",
    "ndtri",
    "spherical_bessel_j0",
    "zeta",
]


@_make_elementwise_unary_reference(
    ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT,
    aten_op=torch.ops.aten.special_bessel_j0,
)
def bessel_j0(a: TensorLikeType) -> TensorLikeType:
    return prims.bessel_j0(a)


@_make_elementwise_unary_reference(
    ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT,
    aten_op=torch.ops.aten.special_bessel_j1,
)
def bessel_j1(a: TensorLikeType) -> TensorLikeType:
    return prims.bessel_j1(a)


@register_decomposition(torch.ops.aten.special_entr)
@out_wrapper()
@elementwise_type_promotion_wrapper(
    type_promoting_args=("a",),
    type_promotion_kind=utils.ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT,
)
def entr(a: TensorLikeType) -> TensorLikeType:
    return torch.where(
        torch.isnan(a),
        a,
        torch.where(a > 0, -a * torch.log(a), torch.where(a == 0, 0, -torch.inf)),
    )


@register_decomposition(torch.ops.aten.special_erfcx)
@out_wrapper()
@elementwise_type_promotion_wrapper(
    type_promoting_args=("a",),
    type_promotion_kind=utils.ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT,
)
def erfcx(a: TensorLikeType) -> TensorLikeType:
    return prims.erfcx(a)


# alias for sigmoid
expit = torch.sigmoid


@_make_elementwise_unary_reference(
    ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT, aten_op=torch.ops.aten.special_i0e
)
def i0e(a: TensorLikeType) -> TensorLikeType:
    return prims.bessel_i0e(a)


@_make_elementwise_unary_reference(
    ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT, aten_op=torch.ops.aten.special_i1
)
def i1(a: TensorLikeType) -> TensorLikeType:
    return prims.bessel_i1(a)


@_make_elementwise_unary_reference(
    ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT, aten_op=torch.ops.aten.special_i1e
)
def i1e(a: TensorLikeType) -> TensorLikeType:
    return prims.bessel_i1e(a)


@register_decomposition(torch.ops.aten.special_log_ndtr)
@out_wrapper()
@elementwise_type_promotion_wrapper(
    type_promoting_args=("a",),
    type_promotion_kind=utils.ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT,
)
def log_ndtr(a: TensorLikeType) -> TensorLikeType:
    # Note: M_SQRT1_2 is the value of 1 / √2
    M_SQRT1_2 = 0.707106781186547524400844362104849039
    t = a * M_SQRT1_2
    return torch.where(
        a < 1.0,
        torch.log(torch.special.erfcx(-t) / 2) - t * t,
        torch.log1p(-refs.erfc(t) / 2),
    )


@register_decomposition(torch.ops.aten.logit)
@out_wrapper()
@elementwise_type_promotion_wrapper(
    type_promoting_args=("self",),
    type_promotion_kind=utils.ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT,
)
def logit(self: TensorLikeType, eps: Optional[float] = None) -> TensorLikeType:
    if eps is None:
        eps = -1.0
    lo = eps
    hi = 1 - eps
    self = torch.clamp(self, lo, hi)
    return torch.log(torch.true_divide(self, torch.sub(1, self)))


def _xlog1py(
    a: Union[TensorLikeType, NumberType], b: Union[TensorLikeType, NumberType]
):
    assert isinstance(a, TensorLike) or isinstance(b, TensorLike)

    # 1) Operations derived from elementwise_unary_scalar_wrapper contain non-ref
    # functions, which causes a runtime error in TorchRefsMode context manager
    # when strict = True
    # 2) where requires all tensors to be on the same device
    if isinstance(a, TensorLike):
        if isinstance(b, Number):
            b = refs.scalar_tensor(b, dtype=a.dtype, device=a.device)
        elif utils.is_cpu_scalar_tensor(b):
            b = prims.device_put(b, device=a.device)
    elif isinstance(b, TensorLike):
        if isinstance(a, Number):
            a = refs.scalar_tensor(a, dtype=b.dtype, device=b.device)
        elif utils.is_cpu_scalar_tensor(a):
            a = prims.device_put(a, device=b.device)

    assert isinstance(a, TensorLike)
    assert isinstance(b, TensorLike)
    rhs = torch.where(torch.eq(a, 0), 0, torch.mul(a, torch.log1p(b)))
    return torch.where(torch.isnan(b), float("nan"), rhs)


# TODO add docstring
xlog1py = _make_elementwise_binary_reference(
    _xlog1py,
    type_promotion_kind=utils.ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT,
    aten_op=torch.ops.aten.special_xlog1py,
)


@register_decomposition(torch.ops.aten.mvlgamma)
@out_wrapper()
@elementwise_type_promotion_wrapper(
    type_promoting_args=("a",),
    type_promotion_kind=utils.ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT,
)
def multigammaln(a: TensorLikeType, p: int) -> TensorLikeType:
    c = 0.25 * p * (p - 1) * math.log(math.pi)
    b = 0.5 * torch.arange(start=(1 - p), end=1, step=1, dtype=a.dtype, device=a.device)
    return torch.sum(torch.lgamma(a.unsqueeze(-1) + b), dim=-1) + c


@register_decomposition(torch.ops.aten.special_ndtr)
@out_wrapper()
@elementwise_type_promotion_wrapper(
    type_promoting_args=("a",),
    type_promotion_kind=utils.ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT,
)
def ndtr(a: TensorLikeType) -> TensorLikeType:
    # Note: M_SQRT1_2 is the value of 1 / √2
    M_SQRT1_2 = 0.707106781186547524400844362104849039
    a_sqrt_2 = a * M_SQRT1_2
    return (1 + torch.erf(a_sqrt_2)) * 0.5


@register_decomposition(torch.ops.aten.special_ndtri)
@out_wrapper()
@elementwise_type_promotion_wrapper(
    type_promoting_args=("a",),
    type_promotion_kind=utils.ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT,
)
def ndtri(a: TensorLikeType) -> TensorLikeType:
    return prims.ndtri(a)


@_make_elementwise_unary_reference(
    ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT,
    aten_op=torch.ops.aten.special_spherical_bessel_j0,
)
def spherical_bessel_j0(a: TensorLikeType) -> TensorLikeType:
    return prims.spherical_bessel_j0(a)


zeta = _make_elementwise_binary_reference(
    prims.zeta,  # type: ignore[has-type]
    type_promotion_kind=utils.ELEMENTWISE_TYPE_PROMOTION_KIND.INT_TO_FLOAT,
    aten_op=torch.ops.aten.special_zeta,
)
