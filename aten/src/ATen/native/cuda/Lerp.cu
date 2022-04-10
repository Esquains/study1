#define TORCH_ASSERT_NO_OPERATORS
#include <ATen/native/Lerp.h>
#include <ATen/Dispatch.h>
#include <ATen/TensorIterator.h>
#include <ATen/native/cuda/Loops.cuh>
#include <ATen/native/cuda/JitLoops.cuh>

namespace at {
namespace native {
namespace {

const char lerp_tensor_name[] = "lerp_kernel";
void lerp_tensor_kernel(at::TensorIteratorBase& iter) {
  auto dtype = iter.common_dtype();
  if(at::isComplexType(dtype)) {
#if AT_USE_JITERATOR()
  static const auto lerp_tensor_string = jiterator_stringify(
      T lerp_tensor_kernel(scalar_t self_val, scalar_t end_val, scalar_t weight_val) {
        template <typename T>
        return (std::abs(weight_val) < 0.5)
            ? self_val + weight_val * (end_val - self_val)
            : end_val -
                (end_val - self_val) * (static_cast<scalar_t>(1) - weight_val);
        }
      ); // lerp_tensor_string
  AT_DISPATCH_COMPLEX_TYPES(dtype, "lerp_cuda", [&] {
        jitted_gpu_kernel<
          /*name=*/ lerp_tensor_name,
          /*return_dtype=*/ scalar_t,
          /*common_dtype=*/ scalar_t,
          /*arity=*/ 3>(iter, lerp_tensor_string);
      });
#else
  AT_DISPATCH_COMPLEX_TYPES(dtype, "lerp_cuda", [&] {
      at::native::gpu_kernel(
        iter,
        [] GPU_LAMBDA(
            scalar_t self_val,
            scalar_t end_val,
            scalar_t weight_val) -> scalar_t {
          return (std:abs(weight_val) < 0.5)
              ? self_val + weight_val * (end_val - self_val)
              : end_val -
                  (end_val - self_val) * (static_cast<scalar_t>(1) - weight_val);
        });
      });
#endif
  } else {
  AT_DISPATCH_FLOATING_TYPES_AND2(
      at::ScalarType::Half, at::ScalarType::BFloat16,
      dtype, "lerp_cuda",
      [&] {
        at::native::gpu_kernel(
            iter,
            [] GPU_LAMBDA(
                scalar_t self_val,
                scalar_t end_val,
                scalar_t weight_val) -> scalar_t {
              return (std::abs(weight_val) < 0.5)
                  ? self_val + weight_val * (end_val - self_val)
                  : end_val -
                      (end_val - self_val) *
                          (static_cast<scalar_t>(1) - weight_val);
            });
      });
  }
}

const char lerp_scalar_name[] = "lerp_kernel";
void lerp_scalar_kernel(at::TensorIteratorBase& iter, const c10::Scalar& weight) {
  auto dtype = iter.common_dtype();
  if (at::isComplexType(dtype)) {
#if AT_USE_JITERATOR()
  static const auto lerp_scalar_string = jiterator_stringify(
      auto weight_val = weight.to<scalar_t>();
      template <typename T>
      T lerp_scalar_kernel(scalar_t self_val, scalar_t end_val) {
        return (std::abs(weight_val) < 0.5)
            ? self_val + weight_val * (end_val - self_val)
            : end_val -
                (end_val - self_val) * (static_cast<scalar_t>(1) - weight_val);
      } // lerp_scalar_string
  );
  AT_DISPATCH_COMPLEX_TYPES(dtype, "lerp_cuda", [&] {
      jitted_gpu_kernel<
        /*name=*/ lerp_scalar_name,
        /*return_dtype=*/ scalar_t,
        /*common_dtype=*/ scalar_t,
        /*arity=*/ 2>(iter, lerp_scalar_string);
  });
#else
  AT_DISPATCH_COMPLEX_TYPES(dtype, "lerp_cuda", [&] {
      auto weight_val = weight.to<scalar_t>();
      gpu_kernel(
        iter,
        [=] GPU_LAMBDA(scalar_t self_val, scalar_t end_val) {
          return (std::abs(weight_val) < 0.5)
              ? self_val + weight_val * (end_val - self_val)
              : end_val -
                  (end_val - self_val) * (static_cast<scalar_t>(1) - weight_val);
         });
  });
#endif
  } else {
  AT_DISPATCH_FLOATING_TYPES_AND2(
      at::ScalarType::Half, at::ScalarType::BFloat16,
      dtype, "lerp_cuda",
      [&]{
        auto weight_val = weight.to<scalar_t>();
        at::native::gpu_kernel(
            iter, [=] GPU_LAMBDA(scalar_t self_val, scalar_t end_val) {
              return (std::abs(weight_val) < 0.5)
                  ? self_val + weight_val * (end_val - self_val)
                  : end_val -
                      (end_val - self_val) * (static_cast<scalar_t>(1) - weight_val);
            });
      });
    }
}

} // anonymous namespace

REGISTER_DISPATCH(lerp_kernel_tensor_weight, &lerp_tensor_kernel);
REGISTER_DISPATCH(lerp_kernel_scalar_weight, &lerp_scalar_kernel);

} // namespace native
} // namespace at
