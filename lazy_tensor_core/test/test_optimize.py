import unittest

from lazy_tensor_core import _LAZYC
_LAZYC._ltc_init_ts_backend()
from lazy_tensor_core.core.optimize import optimize
import torch
from torch import nn
import dis
import sys
import inspect

class ModuleConstScale(nn.Module):
    def __init__(self):
        super(ModuleConstScale, self).__init__()

    def forward(self, a):
        return a * 2

class ModuleSub(nn.Module):
    def __init__(self):
        super(ModuleSub, self).__init__()

    def forward(self, a, b):
        return a - b

class ModuleAddcmul(nn.Module):
    """
    addcmul function takes a at::Scalar which results in a special TSData containing a Scalar rather than a Tensor.
    """
    def __init__(self):
        super(ModuleAddcmul, self).__init__()

    def forward(self, a, b, c):
        return torch.addcmul(a, b, c, value=5)

class ModuleReturnMulti(nn.Module):
    def __init__(self):
        super(ModuleReturnMulti, self).__init__()

    def forward(self, a, b):
        return (b + 1, a - 1)

def gen_rand_args(mod):
    args = []
    for _ in range(len(inspect.signature(mod.forward).parameters)):
        args.append(torch.randn(2, 3))
    return args

def allclose(expected, actual):
    def unwrap(cont):
        if isinstance(cont, (list, tuple)) and len(cont) == 1:
            return cont[0]
        return cont
    expected = unwrap(expected)
    actual = unwrap(actual)

    if isinstance(expected, torch.Tensor) and isinstance(actual, torch.Tensor):
        return torch.allclose(expected, actual)
    elif isinstance(expected, (tuple, list)) and isinstance(actual, (tuple, list)):
        return len(expected) == len(actual) and all(torch.allclose(a, b) for a, b in zip(expected, actual))
    else:
        raise RuntimeError("Unexpected types")

def verify_reusing_compiled_graph(mod, ncase=10):
    args = gen_rand_args(mod)
    out = mod(*args)

    dis.dis(mod.forward)

    optimized_mod = optimize(mod, args)
    print("return value of optimized_mod", optimized_mod(*args))

    # check correctness
    failed_index = []
    for i in range(ncase):
        rand_args = gen_rand_args(mod)
        expected = mod(*rand_args)
        actual = optimized_mod(*rand_args)

        if not allclose(expected, actual):
            failed_index.append(i)

    if len(failed_index) > 0:
        raise RuntimeError(f"Failed {len(failed_index)}/{ncase} cases")

def maketest(module_cls):
    def wrapper(self):
        verify_reusing_compiled_graph(module_cls())

    return wrapper

# import pdb; pdb.set_trace()
class OptimizeTest(unittest.TestCase):
    test_sub = maketest(ModuleSub)
    test_const_scale = maketest(ModuleConstScale)
    test_addcmul = maketest(ModuleAddcmul)
    test_return_multi = maketest(ModuleReturnMulti)
