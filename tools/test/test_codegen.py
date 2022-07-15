import dataclasses
import typing
import unittest

import torchgen.model

from tools.autograd import gen_autograd_functions, load_derivatives
from torchgen.gen import get_native_function_schema_registrations
from torchgen.selective_build.selector import SelectiveBuilder


class TestCreateDerivative(unittest.TestCase):
    def test_named_grads(self) -> None:
        schema = torchgen.model.FunctionSchema.parse(
            "func(Tensor a, Tensor b) -> (Tensor x, Tensor y)"
        )
        native_function = dataclasses.replace(DEFAULT_NATIVE_FUNCTION, func=schema)

        derivative = load_derivatives.create_derivative(
            native_function,
            formula="func_backward(grad_x, grad_y)",
            var_names=(),
            available_named_gradients=["grad_x", "grad_y"],
        )
        self.assertSetEqual(derivative.named_gradients, {"grad_x", "grad_y"})

    def test_non_differentiable_output(self) -> None:
        specification = "func(Tensor a, Tensor b) -> (Tensor x, bool y, Tensor z)"
        schema = torchgen.model.FunctionSchema.parse(specification)
        native_function = dataclasses.replace(DEFAULT_NATIVE_FUNCTION, func=schema)

        differentiability_info = load_derivatives.create_differentiability_info(
            defn={
                "name": specification,
                "a": "grads[0]",
                "b": "grads[2]",
            },
            functions_by_signature={schema.signature(): [native_function]},
            functions_by_schema={specification: native_function},
            op_counter=typing.Counter[str](),
        )

        self.assertSequenceEqual(
            differentiability_info.available_named_gradients,
            # grad_y is not present because y is a
            # bool and thus not differentiable.
            ["grad_x", "grad_z"],
        )

    def test_indexed_grads(self) -> None:
        schema = torchgen.model.FunctionSchema.parse(
            "func(Tensor a, Tensor b) -> (Tensor x, Tensor y)"
        )
        native_function = dataclasses.replace(DEFAULT_NATIVE_FUNCTION, func=schema)

        derivative = load_derivatives.create_derivative(
            native_function,
            formula="func_backward(grads[0], grads[1])",
            var_names=(),
            available_named_gradients=["grad_x", "grad_y"],
        )
        self.assertSetEqual(derivative.named_gradients, set())

    def test_named_grads_and_indexed_grads(self) -> None:
        specification = "func(Tensor a, Tensor b) -> (Tensor x, Tensor y)"
        schema = torchgen.model.FunctionSchema.parse(specification)
        native_function = dataclasses.replace(DEFAULT_NATIVE_FUNCTION, func=schema)

        with self.assertRaisesRegex(
            RuntimeError, 'illegally mixes use of "grad_RETURN_NAME"'
        ):
            load_derivatives.create_differentiability_info(
                defn={
                    "name": specification,
                    # Uh-oh, the derivatives reference gradients by
                    # name and by index.
                    "a": "grad_x",
                    "b": "grads[1]",
                },
                functions_by_signature={schema.signature(): [native_function]},
                functions_by_schema={specification: native_function},
                op_counter=typing.Counter[str](),
            )


class TestGenAutogradFunctions(unittest.TestCase):
    def test_non_differentiable_output_invalid_type(self) -> None:
        specification = "func(Tensor a, Tensor b) -> (Tensor x, bool y, Tensor z)"
        schema = torchgen.model.FunctionSchema.parse(specification)
        native_function = dataclasses.replace(DEFAULT_NATIVE_FUNCTION, func=schema)

        differentiability_info = load_derivatives.create_differentiability_info(
            defn={
                "name": specification,
                "a": "grad_x",
                "b": "grad_z",
            },
            functions_by_signature={schema.signature(): [native_function]},
            functions_by_schema={specification: native_function},
            op_counter=typing.Counter[str](),
        )
        definition = gen_autograd_functions.process_function(
            differentiability_info, gen_autograd_functions.FUNCTION_DEFINITION
        )
        # grad_z should map to grads[1], not grads[2] because output 1
        # (y) is not differentiable.
        assert "grad_z = grads[2]" not in definition
        assert "grad_z = grads[1]" in definition

    def test_non_differentiable_output_output_differentiability(self) -> None:
        specification = "func(Tensor a, Tensor b) -> (Tensor x, Tensor y, Tensor z)"
        schema = torchgen.model.FunctionSchema.parse(specification)
        native_function = dataclasses.replace(DEFAULT_NATIVE_FUNCTION, func=schema)

        differentiability_info = load_derivatives.create_differentiability_info(
            defn={
                "name": specification,
                "a": "grad_x",
                "b": "grad_z",
                "output_differentiability": [True, False, True],
            },
            functions_by_signature={schema.signature(): [native_function]},
            functions_by_schema={specification: native_function},
            op_counter=typing.Counter[str](),
        )
        definition = gen_autograd_functions.process_function(
            differentiability_info, gen_autograd_functions.FUNCTION_DEFINITION
        )
        # grad_z should map to grads[1], not grads[2] because output 1
        # (y) is not differentiable.
        assert "grad_z = grads[2]" not in definition
        assert "grad_z = grads[1]" in definition


class TestGenSchemaRegistration(unittest.TestCase):
    def setUp(self) -> None:
        self.selector = SelectiveBuilder.get_nop_selector()
        self.custom_native_function, _ = torchgen.model.NativeFunction.from_yaml(
            {"func": "custom::func() -> bool"},
            loc=torchgen.model.Location(__file__, 1),
            valid_tags=set(),
        )

    def test_default_namespace_schema_registration_code_valid(self) -> None:
        native_functions = [DEFAULT_NATIVE_FUNCTION]
        registrations, _ = get_native_function_schema_registrations(
            native_functions=native_functions,
            schema_selector=self.selector,
        )
        self.assertEqual(registrations, ['m.def("func() -> bool", {});\n'])

    def test_custom_namespace_schema_registration_code_valid(self) -> None:
        _, registrations = get_native_function_schema_registrations(
            native_functions=[self.custom_native_function],
            schema_selector=self.selector,
        )
        self.assertEqual(
            registrations,
            """
TORCH_LIBRARY(custom, m) {
  m.def("func() -> bool", {});

};""",
        )

    def test_mixed_namespace_schema_registration_code_valid(self) -> None:
        (
            aten_registrations,
            custom_registrations,
        ) = get_native_function_schema_registrations(
            native_functions=[DEFAULT_NATIVE_FUNCTION, self.custom_native_function],
            schema_selector=self.selector,
        )
        self.assertEqual(aten_registrations, ['m.def("func() -> bool", {});\n'])
        self.assertEqual(
            custom_registrations,
            """
TORCH_LIBRARY(custom, m) {
  m.def("func() -> bool", {});

};""",
        )

    def test_3_namespaces_schema_registration_code_invalid(self) -> None:
        custom2_native_function, _ = torchgen.model.NativeFunction.from_yaml(
            {"func": "custom2::func() -> bool"},
            loc=torchgen.model.Location(__file__, 1),
            valid_tags=set(),
        )
        with self.assertRaises(AssertionError):
            get_native_function_schema_registrations(
                native_functions=[
                    DEFAULT_NATIVE_FUNCTION,
                    self.custom_native_function,
                    custom2_native_function,
                ],
                schema_selector=self.selector,
            )


# Represents the most basic NativeFunction. Use dataclasses.replace()
# to edit for use.
DEFAULT_NATIVE_FUNCTION, _ = torchgen.model.NativeFunction.from_yaml(
    {"func": "func() -> bool"},
    loc=torchgen.model.Location(__file__, 1),
    valid_tags=set(),
)


if __name__ == "__main__":
    unittest.main()
