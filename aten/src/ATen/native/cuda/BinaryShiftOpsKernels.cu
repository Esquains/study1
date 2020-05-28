#include <ATen/Dispatch.h>
#include <ATen/native/DispatchStub.h>
#include <ATen/native/cuda/Loops.cuh>
#include <ATen/native/TensorIterator.h>
#include <ATen/native/BinaryOps.h>

// NOTE: CUDA on Windows requires that the enclosing function
// of a __device__ lambda not have internal linkage.

namespace at { namespace native {

#ifdef __HIP_PLATFORM_HCC__
#define ROCm_Bug(x)
#else
#define ROCm_Bug(x) x
#endif

void lshift_kernel_cuda(TensorIterator& iter) {
  if (iter.dtype() == ScalarType::Float ||
      iter.dtype() == ScalarType::Double ||
      iter.dtype() == ScalarType::Half) {
    AT_DISPATCH_FLOATING_TYPES_AND(ScalarType::Half, iter.dtype(), "lshift_cuda", [&]() {
      gpu_kernel_with_scalars(
        iter,
        []GPU_LAMBDA(scalar_t a, scalar_t b) -> scalar_t {
          return a * ROCm_Bug(std)::pow(static_cast<scalar_t>(2), b);
      });
    });
  } else {
    AT_DISPATCH_INTEGRAL_TYPES(iter.dtype(), "lshift_cuda", [&]() {
      gpu_kernel_with_scalars(iter,
        []GPU_LAMBDA(scalar_t a, scalar_t b) -> scalar_t {
          return static_cast<std::make_unsigned_t<scalar_t>>(a) << b;
      });
    });
  }
}

void rshift_kernel_cuda(TensorIterator& iter) {
  if (iter.dtype() == ScalarType::Float ||
      iter.dtype() == ScalarType::Double ||
      iter.dtype() == ScalarType::Half) {
    AT_DISPATCH_FLOATING_TYPES_AND(ScalarType::Half, iter.dtype(), "rshift_cuda", [&]() {
      gpu_kernel_with_scalars(
        iter,
        []GPU_LAMBDA(scalar_t a, scalar_t b) -> scalar_t {
          return a / ROCm_Bug(std)::pow(static_cast<scalar_t>(2), b);
      });
    });
  } else {
    AT_DISPATCH_INTEGRAL_TYPES(iter.dtype(), "rshift_cuda", [&]() {
      gpu_kernel_with_scalars(iter,
        []GPU_LAMBDA(scalar_t a, scalar_t b) -> scalar_t {
          return static_cast<std::make_unsigned_t<scalar_t>>(a) >> b;
      });
    });
  }
}

#undef ROCm_Bug

REGISTER_DISPATCH(lshift_stub, &lshift_kernel_cuda);
REGISTER_DISPATCH(rshift_stub, &rshift_kernel_cuda);

}} // namespace at::native
