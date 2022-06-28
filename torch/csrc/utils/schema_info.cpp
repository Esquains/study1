#include <torch/csrc/utils/schema_info.h>

namespace torch {
namespace utils {

bool SchemaInfo::hasSideEffects() const {
  static std::vector<std::string> side_effects_ops = {
      "aten::warn",
      "aten::save",
      "aten::manual_seed",
      "aten::wait",
      "cuda::set_stream",
      "cuda::_set_device",
      "cuda::_current_device",
      "cuda::synchronize",
  };
  return std::any_of(
      side_effects_ops.begin(),
      side_effects_ops.end(),
      [this](const std::string& side_effect_op) {
        return side_effect_op == this->schema_.name();
      });
}

bool SchemaInfo::isDeterministic() const {
  static const std::vector<const char*> nondeterministic_ops = {
      "aten::dropout(Tensor input, float p, bool train) -> Tensor",
      "aten::_fused_dropout(Tensor self, float p, Generator? generator) -> (Tensor, Tensor)",
      "aten::_standard_gamma(Tensor self, Generator? generator) -> Tensor",
      "aten::bernoulli(Tensor self, *, Generator? generator) -> Tensor",
      "aten::bernoulli(Tensor self, float p, *, Generator? generator) -> Tensor",
      "aten::multinomial(Tensor self, int num_samples, bool replacement, *, Generator? generator) -> Tensor",
      "aten::native_dropout(Tensor input, float p, bool? train) -> (Tensor, Tensor)",
      "aten::normal(Tensor mean, Tensor std, *, Generator? generator) -> Tensor",
      "aten::normal(float mean, Tensor std, *, Generator? generator) -> Tensor",
      "aten::normal(Tensor mean, float std, *, Generator? generator) -> Tensor",
      "aten::poisson(Tensor self, Generator? generator) -> Tensor",
      "aten::binomial(Tensor count, Tensor prob, Generator? generator=None) -> Tensor",
      "aten::rrelu(Tensor self, Scalar lower, Scalar upper, bool training, Generator? generator) -> Tensor",
      "aten::rrelu_with_noise(Tensor self, Tensor noise, Scalar lower, Scalar upper, bool training, Generator? generator) -> Tensor",
      "aten::rand(int[] size, *, int? dtype, int? layout, Device? device, bool? pin_memory) -> Tensor",
      "aten::rand_like(Tensor self, *, int? dtype=None, int? layout=None, Device? device=None, bool? pin_memory=None, MemoryFormat? memory_format=None) -> Tensor",
      "aten::randint(int high, int[] size, *, int? dtype, int? layout, Device? device, bool? pin_memory) -> Tensor",
      "aten::randint(int low, int high, int[] size, *, int? dtype, int? layout, Device? device, bool? pin_memory) -> Tensor",
      "aten::randint_like(Tensor self, int high, *, int? dtype=None, int? layout=None, Device? device=None, bool? pin_memory=None, MemoryFormat? memory_format=None) -> Tensor",
      "aten::randint_like(Tensor self, int low, int high, *, int? dtype=None, int? layout=None, Device? device=None, bool? pin_memory=None, MemoryFormat? memory_format=None) -> Tensor",
      "aten::randn(int[] size, *, int? dtype, int? layout, Device? device, bool? pin_memory) -> Tensor",
      "aten::randn_like(Tensor self, *, int? dtype=None, int? layout=None, Device? device=None, bool? pin_memory=None, MemoryFormat? memory_format=None) -> Tensor",
      "aten::randperm(int n, *, int? dtype, int? layout, Device? device, bool? pin_memory) -> Tensor"};

  return !(std::any_of(
      nondeterministic_ops.begin(),
      nondeterministic_ops.end(),
      [this](const char* nondeterministic_op) {
        return torch::jit::getOperatorForLiteral(nondeterministic_op)
                   ->schema() == this->schema_;
      }));
}

bool SchemaInfo::isMutating(int index) const {
  TORCH_INTERNAL_ASSERT(
      index < schema_.arguments().size() && index >= 0,
      "Invalid index for schema.");

  return schema_.arguments()[index].alias_info() != nullptr &&
      schema_.arguments()[index].alias_info()->isWrite();
}

bool SchemaInfo::isMutating(c10::string_view name) const {
  c10::optional<int> index = schema_.argumentIndexWithName(name);
  TORCH_INTERNAL_ASSERT(
      index != c10::nullopt, "Schema has no argument named ", name);

  return isMutating(*index);
}

std::vector<c10::Argument> SchemaInfo::getCorrectList(
    SchemaArgType type) const {
  if (type == SchemaArgType::input) {
    return schema_.arguments();
  } else {
    return schema_.returns();
  }
}

bool SchemaInfo::areAliasing(
    const SchemaArgument& lhs,
    const SchemaArgument& rhs) const {
  TORCH_INTERNAL_ASSERT(
      (lhs.index < getCorrectList(lhs.type).size() && lhs.index >= 0),
      "Invalid index for schema.");
  TORCH_INTERNAL_ASSERT(
      (rhs.index < getCorrectList(rhs.type).size() && rhs.index >= 0),
      "Invalid index for schema.");

  const c10::Argument lhsArg = getCorrectList(lhs.type)[lhs.index];
  const c10::Argument rhsArg = getCorrectList(rhs.type)[rhs.index];

  if ((lhsArg.alias_info() && lhsArg.alias_info()->isWildcardAfter()) ||
      (rhsArg.alias_info() && rhsArg.alias_info()->isWildcardAfter())) {
    if (lhsArg.type()->kind() == rhsArg.type()->kind()) {
      return true;
    } else {
      for (const auto& type : lhsArg.type()->containedTypes()) {
        if (type->kind() == rhsArg.type()->kind()) {
          return true;
        }
      }
      for (const auto& type : rhsArg.type()->containedTypes()) {
        if (type->kind() == lhsArg.type()->kind()) {
          return true;
        }
      }
    }
  }

  if (lhsArg.alias_info() && rhsArg.alias_info()) {
    for (const auto& lhsSet : lhsArg.alias_info()->afterSets()) {
      for (const auto& rhsSet : rhsArg.alias_info()->afterSets()) {
        if (lhsSet == rhsSet) {
          return true;
        }
      }
    }
  }
  return false;
}
} // namespace utils
} // namespace torch
