"""
pytest -vs test/lazy_scheduler/test_lazy_scheduler.py

pytest -vs test/lazy_scheduler/test_lazy_scheduler.py::TestLazyScheduler::test_backward_simple_no_segment
"""

import torch
from torch.testing._internal.common_utils import TestCase as TorchTestCase
from torch._dynamo import disable
import functools
from torch._inductor.compile_fx import compile_fx


class TestCase(TorchTestCase):
  def setUp(self):
    torch._dynamo.reset()
    super().setUp()

  def tearDown(self):
    super().tearDown()
    torch._dynamo.reset()


class TestLazyScheduler(TestCase):
  def test_backward_simple_no_segment(self):
    class TestModule(torch.nn.Module):
      def __init__(self):
        super().__init__()

      def func1(self, x, y):
        z = torch.matmul(x, y)
        return z

      def forward(self, x, y):
        z = self.func1(x, y)
        z = z * z
        return z

    device = "cuda" if torch.cuda.is_available() else "cpu"

    from torch._lazy_scheduler import LazyScheduler

    m = TestModule()
    m = m.to(device)
    x = torch.randn(4, 4, requires_grad=True, device=device)
    y = torch.randn(4, 4, requires_grad=True, device=device)

    actual_e = m(x, y)
    actual_e.sum().backward()
    print(f"eager: first iter done")
    actual_e = m(x, y)
    actual_e.sum().backward()
    print(f"eager: second iter done")

    lazy_scheduler = LazyScheduler()
    compiled_m_ls = torch.compile(
      m,
      backend=functools.partial(compile_fx, inner_compile=lazy_scheduler.compile),
      fullgraph=False
    )

    actual_ls = compiled_m_ls(x, y)
    print(f"actual_ls: {actual_ls}")
    actual_ls.sum().backward()
    print(f"compiled_ls: first iter done")
    actual_ls = compiled_m_ls(x, y)
    print(f"actual_ls: {actual_ls}")
    actual_ls.sum().backward()
    print(f"compiled_ls: second iter done")

if __name__ == "__main__":
    from torch._dynamo.test_case import run_tests
    run_tests()



# TORCH_LOGS="+dynamo,aot,inductor" TORCH_COMPILE_DEBUG=1 python test/test_yf225.py


"""
TODO: high-pri tests:
- [Added in combined test] in-place ops within named segment
- [Added in combined test] mix of named and unnamed segments (unnamed segment in-between named segments)
- [Added in combined test] reordering named segments across unnamed segments (N1, U1, N2 becomes N2, U1, N1)
- graph break within named segment
- graph break within unnamed segment
- nested segments (not supported for now)
- no recursive module method calls
- TestModule_instance_a.func1 calling TestModule_instance_b.func1 (which is a named segment) is allowed

Should already work, just need unit tests:
- reordering named segments across multiple named segments
- reordering named segments that are next to each other

- only in-place ops within unnamed segment (this is buggy and doesn't work right now)
"""



# TODO: can we use module hook to implement the segment tagging logic?
#
# class Segment:
#     _instance = None
#     def __call__(self, fn, tag):
#         def _fn(*args, **kwargs):
#             with tag_segment(tag):  # TODO: does this work with Dynamo? what if `fn` is not single graph?
#                 return fn(*args, **kwargs)
#         return _fn
#
# def hook(module, args):
#     module.func1 = Segment()(module.func1, "func1")
#     module.func2 = Segment()(module.func2, "func2")
#
# m.register_forward_pre_hook(hook)



"""
@disable()
def g1_mutation_tuple(d, e):
    d.relu_()
    return d, e

@disable()
def g1_mutation_tensor(d, e):
    d.relu_()
    return d + e

@disable()
def g2(a, b):
    return torch.cat(torch.chunk(a * b, 2))

global_a = torch.randn(4, 4, device=device)

@disable()
def g2_read_global_var(a, b):
    return torch.cat(torch.chunk(a * b.div(torch.selu(global_a)), 2))

def global3(a, b):
    return a + b

class TestModule(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.weight = torch.nn.Parameter(torch.randn(1))  # torch.randn(4, 4))
        self.register_buffer('buf', torch.randn(1))  # torch.randn(4, 4))

    @disable()
    def f_read_param_mutate_param(self, c):
        self.buf.relu_()
        return c * c * self.weight

    def f2(self, x, y):
        return x + y

    def subfunc0(self, x, y):
        return x + y

    def subfunc01(self, x, y):
        return x + y

    def func1(self, x, y):
      x.relu_()
      self.buf.relu_()
      y = torch.cat(torch.chunk(y, 2))
      return x, y

    def func2(self, x, y):
        # z = torch.relu(x) + g1_mutation_tuple(x, y)[0]
        # z = z + g1_mutation_tensor(x, x)
        # z = z + g2(x, y)
        # z = x + y
        # z = z + g2_read_global_var(x, y)
        # z = z + self.f_read_param_mutate_param(x)
        # z = z + torch.tanh(self.weight)
        # z = z + self.buf
        # z = z + global_a
        # z = z + self.f2(x, y)
        # z = z + global3(x, y)
        z = x + y
        return z

    def func3(self, x, y):
        k = x * y
        return k

    def forward(self, x, y):
        # z = self.subfunc0(x, y)
        # y = y.relu()
        # z = self.subfunc01(z, y)
        # return z
        x, y = self.func1(x, y)
        # y.relu_()
        y = y.relu()
        z = self.func2(x, y)
        k = self.func3(x, y)  # we should be able to schedule this one before `func2`
        # z.relu_()
        # z = z.relu()
        return z + k
"""
