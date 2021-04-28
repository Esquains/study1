#pragma once
#include <ATen/ATen.h>

#include <c10/util/irange.h>

namespace at {
namespace native {
namespace {
// Check if tensor list has either a boolean tensor or a integer tensor
bool has_int_or_bool_tensor(TensorList tensors) {
  bool has_integral{false};
  for (const auto & tensor : tensors) {
    if (at::isIntegralType(tensor.scalar_type(), /* includeBool= */true)) {
      has_integral = true;
    }
  }
  return has_integral;
}

// Check foreach API restrictions
// - Tensor lists must be non-empty.
// - All TensorLists and ScalarLists must have the same number of elements.
// - Corresponding tensors must have the same size.
// - [optional] All tensors in all lists must have the same dtype.
//     This condition is now checked by `check_fast_path_restrictions`.
//     In general, `foreach` functions go through fast path if possible, otherwise slow path.
//     If a function goes through a slow path, tensors can have different dtypes because
//     a slow path is basically either `for (auto & t : tensors) at::func(t)`.
//     or `for (auto & t : tensors) t.op_()`.
//     But this is MUST for some cases, for example,
//     `_amp_foreach_non_finite_check_and_unscale_cuda_`.
void check_foreach_api_restrictions(TensorList tensors, const bool check_dtype=false) {
  TORCH_CHECK(tensors.size() > 0, "Tensor list must have at least one tensor.");
  if (check_dtype) {
    const auto expected_dtype = tensors[0].dtype();
    for (const auto & t : tensors) {
      TORCH_CHECK(t.dtype() == expected_dtype, "All tensors in the tensor list must have the same dtype.");
    }
  }
}

void check_foreach_api_restrictions(TensorList tensors, ArrayRef<Scalar> scalars, const bool check_dtype=false) {
  check_foreach_api_restrictions(tensors, check_dtype);
  TORCH_CHECK(tensors.size() == scalars.size(), "Tensor list must have same number of elements as scalar list.");
}

void check_foreach_api_restrictions(TensorList tensors1, TensorList tensors2, const bool check_dtype=false) {
  TORCH_CHECK(tensors1.size() > 0, "Tensor list must have at least one tensor.");
  TORCH_CHECK(tensors2.size() > 0, "Tensor list must have at least one tensor.");
  TORCH_CHECK(tensors1.size() == tensors2.size(), "Tensor lists must have the same number of tensors, got ", tensors1.size(), " and ", tensors2.size());

  const auto expected_dtype = tensors1[0].dtype();

  for (const auto i : c10::irange(tensors1.size())) {
    TORCH_CHECK(tensors1[i].sizes() == tensors2[i].sizes(), "Corresponding tensors in lists must have the same size, got ", tensors1[i].sizes(), " and ", tensors2[i].sizes());

    if (check_dtype) {
      TORCH_CHECK(tensors1[i].dtype() == expected_dtype, "All tensors in the tensor list must have the same dtype");
      TORCH_CHECK(tensors2[i].dtype() == expected_dtype, "All tensors in the tensor list must have the same dtype");
    }
  }
}

void check_foreach_api_restrictions(TensorList tensors1, TensorList tensors2, TensorList tensors3, const bool check_dtype=false) {
  TORCH_CHECK(tensors1.size() > 0, "Tensor list must have at least one tensor.");
  TORCH_CHECK(tensors2.size() > 0, "Tensor list must have at least one tensor.");
  TORCH_CHECK(tensors3.size() > 0, "Tensor list must have at least one tensor.");
  TORCH_CHECK(tensors1.size() == tensors2.size(), "Tensor lists must have the same number of tensors, got ", tensors1.size(), " and ", tensors2.size());
  TORCH_CHECK(tensors1.size() == tensors3.size(), "Tensor lists must have the same number of tensors, got ", tensors1.size(), " and ", tensors3.size());

  const auto expected_dtype = tensors1[0].dtype();

  for (const auto i : c10::irange(tensors1.size())) {
    TORCH_CHECK(tensors1[i].sizes() == tensors2[i].sizes(), "Corresponding tensors in lists must have the same size, got ", tensors1[i].sizes(), " and ", tensors2[i].sizes());
    TORCH_CHECK(tensors1[i].sizes() == tensors3[i].sizes(), "Corresponding tensors in lists must have the same size, got ", tensors1[i].sizes(), " and ", tensors3[i].sizes());

    if (check_dtype) {
      TORCH_CHECK(tensors1[i].dtype() == expected_dtype, "All tensors in the tensor list must have the same dtype");
      TORCH_CHECK(tensors2[i].dtype() == expected_dtype, "All tensors in the tensor list must have the same dtype");
      TORCH_CHECK(tensors3[i].dtype() == expected_dtype, "All tensors in the tensor list must have the same dtype");
    }
  }
}

void check_foreach_api_restrictions(TensorList tensors1, TensorList tensors2, TensorList tensors3, ArrayRef<Scalar> scalars, const bool check_dtype=false) {
  check_foreach_api_restrictions(tensors1, tensors2, tensors3, check_dtype);
  TORCH_CHECK(tensors1.size() == scalars.size(), "Tensor list must have same number of elements as scalar list, got ", tensors1.size(), " and ", scalars.size());
}

// To go via 'fast' path, several conditions must be satisfied
// - All tensors in all lists must have the same dtype.
// - All tensors must be on the same device
// - All tensors must have strided layout
// - All tensors must be non-overlapping and dense
// - Resulting tensor must have the same dtype as the input one

bool will_promote_tensor(const Tensor& tensor, const Scalar& scalar, bool does_op_promote_integer_inputs_to_float = false) {
  // complex scalar + float/int/bool tensor will result in complex tensor
  if (scalar.isComplex() && (at::isIntegralType(tensor.scalar_type(), /* includeBool= */true) || at::isFloatingType(tensor.scalar_type())) ) {
    return true;
  }

  // float scalar + int/bool tensor will result in float tensor
  if (scalar.isFloatingPoint() && at::isIntegralType(tensor.scalar_type(), /* includeBool= */true)) {
    return true;
  }

  // int scalar + bool tensor will result in int tensor
  if (scalar.isIntegral(/* includeBool= */false) && tensor.dtype() == at::kBool) {
    return true;
  }

  // In case of division, integer inputs will result in float
  if (does_op_promote_integer_inputs_to_float) {
    if (at::isIntegralType(tensor.scalar_type(), /*includeBool*/ true)) {
      return true;
    }
  }
  auto result_dtype = at::result_type(tensor, scalar);
  return result_dtype != tensor.scalar_type();
}

// Please, make sure to call check_foreach_api_restrictions before calling this method.
// There is a set of preconditions that have to be satisfied.
bool check_fast_path_restrictions(
  ArrayRef<TensorList> tensorLists,
  ArrayRef<Scalar> scalarList = {},
  bool does_op_promote_integer_inputs_to_float = false) {
    const auto expected_dtype = tensorLists[0][0].dtype();
    const auto expected_device = tensorLists[0][0].device();

    auto is_tensor_okay = [&](const Tensor& tensor) {
      return tensor.dtype() == expected_dtype &&
             tensor.device() == expected_device &&
             tensor.layout() == at::kStrided &&
             tensor.is_non_overlapping_and_dense();
    };

    for (const auto& tensorList : tensorLists) {
      for (const auto& tensor : tensorList) {
        if (!is_tensor_okay(tensor)) {
          return false;
        }
      }
    }

    // Check if corresponding tensors in tensor lists have the same strides.
    for (int i=0; i < tensorLists.size(); i++) {
      for (int j=0; j < tensorLists[0].size(); j++) {
        if (tensorLists[0][j].strides() != tensorLists[i][j].strides()) {
          return false;
        }
      }
    }

    // For all j, tensorList[j][0] have the same shape and dtype. (this was a precondition
    // checked by `check_foreach_api_restrictions`). This means we only need to check if
    // {tensorList[0][0], tensorList[0][1], tensorList[0][2], ...} do type promotion with scalarLIst.
    for (int i=0; i < tensorLists[0].size(); i++) {
      if (does_op_promote_integer_inputs_to_float) {
        if (at::isIntegralType(tensorLists[0][i].scalar_type(), /*includeBool*/ true)) {
          return false;
        }
      }

      if (scalarList.size() == 1) {
        if (will_promote_tensor(tensorLists[0][i], scalarList[0])) {
          return false;
        }
      } else if (scalarList.size() > 1) {
        // Complex scalar list is not supported due to the limit for kernel launch argument (4KB)
        if (scalarList[i].isComplex()) {
          return false;
        }

        if (will_promote_tensor(tensorLists[0][i], scalarList[i])) {
          return false;
        }
      }
    }

    return true;
}

bool can_use_fast_route(ArrayRef<TensorList> tensorLists,
                        ArrayRef<Scalar> scalarList = {},
                        bool does_op_promote_integer_inputs_to_float = false) {
#ifdef __HIP_PLATFORM_HCC__
  return false;
#else
  return check_fast_path_restrictions(tensorLists, scalarList, does_op_promote_integer_inputs_to_float);
#endif
}

bool can_use_fast_route(TensorList tensors1, TensorList tensors2, bool does_op_promote_integer_inputs_to_float = false) {
#ifdef __HIP_PLATFORM_HCC__
  return false;
#else
  return can_use_fast_route({tensors1, tensors2}, {}, does_op_promote_integer_inputs_to_float);
#endif
}

}
}} // at::native
