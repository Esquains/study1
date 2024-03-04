# Owner(s): ["module: functorch"]
import unittest

import torch
import torch._dynamo
import torch._inductor
import torch._inductor.decomposition
from torch._functorch.aot_autograd import aot_export_module
from torch._higher_order_ops.effects import with_effects
from torch._higher_order_ops.torchbind import enable_torchbind_tracing
from torch.fx.experimental.proxy_tensor import make_fx
from torch.testing import FileCheck
from torch.testing._internal.common_utils import (
    find_library_location,
    IS_FBCODE,
    IS_MACOS,
    IS_SANDCASTLE,
    IS_WINDOWS,
    run_tests,
    skipIfTorchDynamo,
    TestCase,
)


class TestWithEffects(TestCase):
    def setUp(self):
        if IS_MACOS:
            raise unittest.SkipTest("non-portable load_library call used in test")
        elif IS_SANDCASTLE or IS_FBCODE:
            torch.ops.load_library(
                "//caffe2/test/cpp/jit:test_custom_class_registrations"
            )
        elif IS_WINDOWS:
            lib_file_path = find_library_location("torchbind_test.dll")
            torch.ops.load_library(str(lib_file_path))
        else:
            lib_file_path = find_library_location("libtorchbind_test.so")
            torch.ops.load_library(str(lib_file_path))

    def test_print(self):
        class M(torch.nn.Module):
            def forward(self, x):
                torch.ops.aten._print("moo")
                res = x + x
                torch.ops.aten._print("moo")
                return (res,)

        inputs = (torch.randn(3),)

        # Without functionalization, print should just appear in the graph directly
        gm = make_fx(M())(*inputs)
        FileCheck().check_count("torch.ops.aten._print.default", 2, exactly=True).run(
            gm.code
        )

        # With functionalization, it should appear wrapped with with_effects()
        gm, gs = aot_export_module(M(), inputs, trace_joint=False)
        self.assertExpectedInline(
            str(gm.code).strip(),
            """\
def forward(self, arg0_1, arg1_1):
    with_effects = torch._higher_order_ops.effects.with_effects(arg0_1, torch.ops.aten._print.default, 'moo');  arg0_1 = None
    getitem = with_effects[0];  with_effects = None
    add = torch.ops.aten.add.Tensor(arg1_1, arg1_1);  arg1_1 = None
    with_effects_1 = torch._higher_order_ops.effects.with_effects(getitem, torch.ops.aten._print.default, 'moo');  getitem = None
    getitem_2 = with_effects_1[0];  with_effects_1 = None
    return (getitem_2, add)""",
        )
        self.assertEqual(len(gs.input_tokens), 1)
        self.assertEqual(len(gs.output_tokens), 1)

    @unittest.expectedFailure  # Will enable this once we enable tokens in export
    def test_torchbind_custom_op(self):
        class M(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.attr = torch.classes._TorchScriptTesting._Foo(10, 20)

            def forward(self, x):
                return (x + torch.ops._TorchScriptTesting.takes_foo(self.attr, x),)

        with enable_torchbind_tracing():
            gm, gs = aot_export_module(M(), (torch.ones(2, 3),), trace_joint=False)

        self.assertExpectedInline(
            str(gm.code).strip(),
            """\
def forward(self, arg0_1):
    _tensor_constant0 = self._tensor_constant0
    takes_foo = torch.ops._TorchScriptTesting.takes_foo.default(_tensor_constant0, arg0_1);  _tensor_constant0 = None
    add = torch.ops.aten.add.Tensor(arg0_1, takes_foo);  arg0_1 = takes_foo = None
    return (add,)""",  # noqa: B950
        )
        self.assertEqual(len(gs.input_tokens), 1)
        self.assertEqual(len(gs.output_tokens), 1)

    def test_print_with_buffer_mutations(self):
        class M(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.register_buffer("buf", torch.ones(3))

            def forward(self, x):
                torch.ops.aten._print("moo")
                res = x + x
                self.buf.add_(res)
                res = self.buf + x
                torch.ops.aten._print("moo")
                return (res,)

        inputs = (torch.randn(3),)

        # With functionalization, it should appear wrapped with with_effects()
        gm, gs = aot_export_module(M(), inputs, trace_joint=False)
        self.assertExpectedInline(
            str(gm.code).strip(),
            """\
def forward(self, arg0_1, arg1_1, arg2_1):
    with_effects = torch._higher_order_ops.effects.with_effects(arg0_1, torch.ops.aten._print.default, 'moo');  arg0_1 = None
    getitem = with_effects[0];  with_effects = None
    add = torch.ops.aten.add.Tensor(arg2_1, arg2_1)
    add_1 = torch.ops.aten.add.Tensor(arg1_1, add);  arg1_1 = add = None
    add_2 = torch.ops.aten.add.Tensor(add_1, arg2_1);  arg2_1 = None
    with_effects_1 = torch._higher_order_ops.effects.with_effects(getitem, torch.ops.aten._print.default, 'moo');  getitem = None
    getitem_2 = with_effects_1[0];  with_effects_1 = None
    return (getitem_2, add_1, add_2)""",
        )
        self.assertEqual(len(gs.input_tokens), 1)
        self.assertEqual(len(gs.output_tokens), 1)
        self.assertEqual(len(gs.buffers_to_mutate), 1)

    def test_print_with_input_mutations(self):
        class M(torch.nn.Module):
            def __init__(self):
                super().__init__()

            def forward(self, x):
                torch.ops.aten._print("moo")
                res = x + x
                x.add_(res)
                res = x + x
                torch.ops.aten._print("moo")
                return (res,)

        inputs = (torch.randn(3),)

        # With functionalization, it should appear wrapped with with_effects()
        gm, gs = aot_export_module(M(), inputs, trace_joint=False)
        self.assertEqual(len(gs.input_tokens), 1)
        self.assertEqual(len(gs.output_tokens), 1)
        self.assertEqual(len(gs.user_inputs_to_mutate), 1)

    def test_alias_op(self):
        def f(token, x):
            token, out = with_effects(token, torch.ops.aten.absolute_.default, x)
            return token, out

        with self.assertRaisesRegex(
            AssertionError, r"Ops with aliasing is not supported"
        ):
            make_fx(f)(torch.tensor([]), torch.tensor(4))

    @skipIfTorchDynamo
    def test_compile_aot_eager(self):
        def f(x):
            torch.ops.aten._print("moo")
            res = x + x
            torch.ops.aten._print("moo")
            return res

        inputs = (torch.randn(2, 3),)

        res = torch.compile(f, backend="aot_eager")(*inputs)
        self.assertTrue(torch.allclose(res, f(*inputs)))

    @skipIfTorchDynamo(
        "We're testing if the test works with inductor, which it currently"
        "doesn't, so we expectedFailure-d the test, but the Dynamo tests"
        "override the backend, causing an unexpected success"
    )
    @unittest.expectedFailure  # NYI: AssertionError: with_effects is not an OpOverload
    def test_compile_inductor(self):
        def f(x):
            torch.ops.aten._print("moo")
            res = x + x
            torch.ops.aten._print("moo")
            return res

        inputs = (torch.randn(2, 3),)

        res = torch.compile(f, backend="inductor")(*inputs)
        self.assertTrue(torch.allclose(res, f(*inputs)))

    @skipIfTorchDynamo
    def test_compile_aot_eager_requires_grad(self):
        def f(x):
            torch.ops.aten._print("moo")
            res = x + x
            torch.ops.aten._print("moo")
            return res

        inputs = (torch.randn(2, 3, requires_grad=True),)

        res = torch.compile(f, backend="aot_eager")(*inputs)
        self.assertTrue(torch.allclose(res, f(*inputs)))


if __name__ == "__main__":
    run_tests()
