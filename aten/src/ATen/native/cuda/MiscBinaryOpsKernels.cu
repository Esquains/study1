#include <ATen/native/cuda/Loops.cuh>
#include <ATen/native/TensorIterator.h>
#include <ATen/native/BinaryOps.h>
#include <limits>


// NOTE: CUDA on Windows requires that the enclosing function
// of a __device__ lambda not have internal linkage.

namespace at { namespace native {

void atan2_kernel_cuda(TensorIterator& iter) {
  AT_DISPATCH_FLOATING_TYPES_AND_HALF(iter.common_dtype(), "atan2_cuda", [&]() {
    gpu_kernel_with_scalars(iter, []GPU_LAMBDA(scalar_t a, scalar_t b) -> scalar_t {
      return ::atan2(a, b);
    });
  });
}

void logical_xor_kernel_cuda(TensorIterator& iter) {
  if (iter.common_dtype() == ScalarType::Bool) {
    AT_DISPATCH_ALL_TYPES_AND2(kHalf, kBool, iter.input_dtype(), "logical_xor_cuda", [&]() {
      gpu_kernel_with_scalars(iter, []GPU_LAMBDA(scalar_t a, scalar_t b) -> bool {
        return bool(a) != bool(b);
      });
    });
  } else {
    AT_DISPATCH_ALL_TYPES_AND(kHalf, iter.common_dtype(), "logical_xor_cuda", [&]() {
      gpu_kernel_with_scalars(iter, []GPU_LAMBDA(scalar_t a, scalar_t b) -> scalar_t {
        return static_cast<scalar_t>(bool(a) != bool(b));
      });
    });
  }
}

void smooth_l1_kernel_cuda(TensorIterator& iter) {
  AT_DISPATCH_ALL_TYPES_AND(kHalf, iter.dtype(), "smooth_l1_cuda", [&]() {
    gpu_kernel(iter, [] GPU_LAMBDA(scalar_t a, scalar_t b) -> scalar_t {
      auto z = fabs(a - b);
      return z < scalar_t(1.) ? scalar_t(0.5) * z * z : z - scalar_t(0.5);
    });
  });
}

void mse_kernel_cuda(TensorIterator& iter) {
  AT_DISPATCH_FLOATING_TYPES_AND_HALF(iter.dtype(), "mse_cuda", [&]() {
    gpu_kernel(iter, []GPU_LAMBDA(scalar_t a, scalar_t b) -> scalar_t {
      auto diff = a - b;
      return diff * diff;
    });
  });
}

}} // namespace at::native
