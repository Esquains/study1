#pragma once
#include <ATen/ATen.h>
#include <THC/THCAtomics.cuh>

namespace at {
namespace native {

__device__ __forceinline__ size_t
idx_1d(
    const size_t n, const size_t c, const size_t h,
    const size_t channel, const size_t height){
  return (n * channel + c) * height + h;
}

__device__ __forceinline__ size_t
idx_2d(const size_t nc,
    const size_t h,
    const size_t w,
    const size_t height,
    const size_t width) {
  return (nc * height + h) * width + w;
}

// for channels-last
__device__ __forceinline__ size_t
idx_2d_cl(
  const size_t n, const size_t h, const size_t w, const size_t c,
  const size_t height, const size_t width, const size_t channel) {
  return ((n * height + h) * width + w) * channel + c;
}

__device__ __forceinline__ size_t
idx_3d(
    const size_t n, const size_t c, const size_t d, const size_t h, const size_t w,
    const size_t channel, const size_t depth, const size_t height, const size_t width) {
  return (((n * channel + c) * depth + d) * height + h) * width + w;
}

template <
    typename scalar_t,
    typename std::enable_if<std::is_same<c10::Half, scalar_t>::value>::type* =
        nullptr>
__device__ __forceinline__ void fastSpecializedAtomicAdd(
    scalar_t* tensor,
    size_t index,
    const size_t numel,
    scalar_t value) {
#if (                         \
    (CUDA_VERSION < 10000) || \
    (defined(__CUDA_ARCH__) && (__CUDA_ARCH__ < 700)))
  gpuAtomicAdd(
      reinterpret_cast<at::Half*>(tensor) + index,
      static_cast<at::Half>(value));
#else
  // Accounts for the chance tensor falls on an odd 16 bit alignment (ie, not 32 bit aligned)
  __half* target_addr = reinterpret_cast<__half*>(tensor + index);
  bool low_byte = (reinterpret_cast<std::uintptr_t>(target_addr) % sizeof(__half2) == 0);

  if (low_byte && index < (numel - 1)) {
    __half2 value2;
    value2.x = value;
    value2.y = __int2half_rz(0);
    atomicAdd(reinterpret_cast<__half2*>(target_addr), value2);

  } else if (!low_byte && index > 0) {
    __half2 value2;
    value2.x = __int2half_rz(0);
    value2.y = value;
    atomicAdd(reinterpret_cast<__half2*>(target_addr - 1), value2);

  } else {
    atomicAdd(
        reinterpret_cast<__half*>(tensor) + index, static_cast<__half>(value));
  }
#endif
}

template <
    typename scalar_t,
    typename std::enable_if<!std::is_same<c10::Half, scalar_t>::value>::type* =
        nullptr>
__device__ __forceinline__ void fastSpecializedAtomicAdd(
    scalar_t* tensor,
    size_t index,
    const size_t numel,
    scalar_t value) {
  gpuAtomicAdd(tensor + index, value);
}

template <class scalar_t>
__device__ __forceinline__ void fastAtomicAdd(
    scalar_t* tensor,
    size_t index,
    const size_t numel,
    scalar_t value,
    bool fast_atomics) {
  if (fast_atomics) {
    fastSpecializedAtomicAdd(tensor, index, numel, value);
  } else {
    gpuAtomicAdd(tensor + index, value);
  }
}

} // namespace native
} // namespace at
