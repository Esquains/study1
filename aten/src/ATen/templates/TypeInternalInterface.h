#pragma once
#include <ATen/Type.h>

namespace at {

struct AT_API TypeInternalInterface : public Type {
  explicit TypeInternalInterface(TensorTypeId type_id, bool is_variable, bool is_undefined)
      : Type(type_id, is_variable, is_undefined) {}
  ${pure_virtual_internal_type_method_declarations}
};

} // namespace at
