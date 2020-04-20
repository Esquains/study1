#include "function.h"
#include <ATen/core/op_registration/op_registration.h>
#include <torch/csrc/jit/runtime/instruction.h>
#include <torch/csrc/jit/runtime/operator.h>
#include <torch/csrc/jit/runtime/vararg_functions.h>
#include <torch/custom_class_detail.h>
#include "interpreter.h"

namespace torch {
namespace jit {

char const* toString(OpCode op);
namespace mobile {
Function::Function(c10::QualifiedName name)
    : name_(name), code_(std::make_shared<Code>()) {}

void Function::append_instruction(OpCode op, int X, int N) {
  TORCH_CHECK(
      isOpSupportedInMobile(op),
      toString(op),
      " is not supported in mobile module.");
  code_->instructions_.emplace_back(op, X, N);
}

bool Function::append_operator(
    const std::string& name,
    const std::string& overload_name) {
  // Keep the original opname in code_
  code_->op_names_.emplace_back(name, overload_name);
  auto opname = code_->op_names_.back();

  auto opname_c10 = opname;
  std::function<void(Stack&)> fn;

  // Add "_" prefix to work around the double registration, for operators
  // registered in register_mobile_ops.cpp.
  // TODO: remove it when we migrate all c10 ops.
  if (opname_c10.name != "aten::Int") {
    opname_c10.name = "_" + opname_c10.name;
  }
  auto op = c10::Dispatcher::singleton().findSchema(opname_c10);
  if (op.has_value()) {
    fn = [op](Stack& stack) {
      c10::Dispatcher::singleton().callBoxed(*op, &stack);
    };
  } else { // Not found in c10 registration, use JIT dispatch
    auto jit_op = findOperatorFor(opname);
    TORCH_CHECK(
        jit_op, opname.name, ".", opname.overload_name, " cannot be found.");
    fn = [jit_op](Stack& stack) { jit_op->getOperation()(stack); };
  }

  code_->operators_.emplace_back(fn);
  return true;
}

void Function::append_constant(const c10::IValue& constant) {
  code_->constants_.push_back(constant);
}

void Function::append_type(const at::TypePtr& type) {
  code_->types_.push_back(type);
}

void Function::set_register_size(size_t size) {
  code_->register_size_ = size;
}

bool Function::run(Stack& stack) const {
  InterpreterState interp_state(code_);
  return interp_state.run(stack);
}
} // namespace mobile
} // namespace jit
} // namespace torch
