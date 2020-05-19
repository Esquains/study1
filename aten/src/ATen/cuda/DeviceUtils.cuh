#include <cuda.h>
#include <c10/util/complex_type.h>
#include <c10/util/Half.h>

__device__ __forceinline__ unsigned int ACTIVE_MASK()
{
#ifndef __HIP_PLATFORM_HCC__
    return __activemask();
#else
// will be ignored anyway
    return 0xffffffff;
#endif
}

#if defined(__HIP_PLATFORM_HCC__)
__device__ __forceinline__ unsigned long long int WARP_BALLOT(int predicate)
{
return __ballot(predicate);
}
#else
__device__ __forceinline__ unsigned int WARP_BALLOT(int predicate, unsigned int mask = 0xffffffff)
{
#ifndef __HIP_PLATFORM_HCC__
    return __ballot_sync(mask, predicate);
#else
    return __ballot(predicate);
#endif
}
#endif

template <typename T>
__device__ __forceinline__ T WARP_SHFL_XOR(T value, int laneMask, int width = warpSize, unsigned int mask = 0xffffffff)
{
#ifndef __HIP_PLATFORM_HCC__
    return __shfl_xor_sync(mask, value, laneMask, width);
#else
    return __shfl_xor(value, laneMask, width);
#endif
}

template <typename T>
__device__ __forceinline__ T WARP_SHFL(T value, int srcLane, int width = warpSize, unsigned int mask = 0xffffffff)
{
#ifndef __HIP_PLATFORM_HCC__
    return __shfl_sync(mask, value, srcLane, width);
#else
    return __shfl(value, srcLane, width);
#endif
}

template <typename T>
__device__ __forceinline__ T WARP_SHFL_UP(T value, unsigned int delta, int width = warpSize, unsigned int mask = 0xffffffff)
{
#ifndef __HIP_PLATFORM_HCC__
    return __shfl_up_sync(mask, value, delta, width);
#else
    return __shfl_up(value, delta, width);
#endif
}

template <typename T>
__device__ __forceinline__ T WARP_SHFL_DOWN(T value, unsigned int delta, int width = warpSize, unsigned int mask = 0xffffffff)
{
#ifndef __HIP_PLATFORM_HCC__
    return __shfl_down_sync(mask, value, delta, width);
#else
    return __shfl_down(value, delta, width);
#endif
}

template<>
__device__ __forceinline__ c10::Half WARP_SHFL_DOWN<c10::Half>(c10::Half value, unsigned int delta, int width, unsigned int mask)
{
  return WARP_SHFL_DOWN<__half>(__half(value), delta, width, mask);
}

template <typename T>
__device__ __forceinline__ c10::complex<T> WARP_SHFL_DOWN(c10::complex<T> value, unsigned int delta, int width = warpSize, unsigned int mask = 0xffffffff)
{
#ifndef __HIP_PLATFORM_HCC__
    return c10::complex<T>(
        __shfl_down_sync(mask, value.storage[0], delta, width),
        __shfl_down_sync(mask, value.storage[1], delta, width));
#else
    return c10::complex<T>(
        __shfl_down(value.storage[0], delta, width),
        __shfl_down(value.storage[1], delta, width));
#endif
}

#ifdef __HIP_PLATFORM_HCC__
template<>
__device__ __forceinline__ int64_t WARP_SHFL_DOWN<int64_t>(int64_t value, unsigned int delta, int width , unsigned int mask)
{
  //(HIP doesn't support int64_t). Trick from https://devblogs.nvidia.com/faster-parallel-reductions-kepler/
  int2 a = *reinterpret_cast<int2*>(&value);
  a.x = __shfl_down(a.x, delta);
  a.y = __shfl_down(a.y, delta);
  return *reinterpret_cast<int64_t*>(&a);
}
template<>
__device__ __forceinline__ __half WARP_SHFL_DOWN<__half>(__half value, unsigned int delta, int width, unsigned int mask)
{
  //(HIP doesn't support c10::Half). Trick from https://devblogs.nvidia.com/faster-parallel-reductions-kepler/
  short a = *reinterpret_cast<short*>(&value);
  a = __shfl_down(a, delta);
  return *reinterpret_cast<__half*>(&a);
}
#endif
