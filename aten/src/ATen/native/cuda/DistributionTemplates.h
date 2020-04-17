#pragma once

#include <ATen/AccumulateType.h>
#include <ATen/Dispatch.h>
#include <ATen/native/TensorIterator.h>
#include <ATen/native/cuda/Loops.cuh>
#include <c10/util/Half.h>
#include <ATen/cuda/CUDAContext.h>
#include <ATen/cuda/detail/OffsetCalculator.cuh>
#include <ATen/detail/FunctionTraits.h>

#include <curand.h>
#include <curand_kernel.h>
#include <curand_philox4x32_x.h>
#include <cstdint>
#include <limits>
#include <utility>
#include <mutex>
#include <tuple>
#include <type_traits>

namespace {

// launch bounds used for kernels utilizing TensorIterator
const uint32_t block_size_bound = 256;
const uint32_t grid_size_bound = 4;
// number of randoms given by distributions like curand_uniform4, curand_uniform2_double
// used in calculating philox offset.
const uint32_t curand4_engine_calls = 4;

// utility function that calculates proper philox_offset
// for distributions utilizing TensorIterator. For distributions using
// TensorIterator, we are using a grid-stride loop with each
// thread yielding one element per thread. For the edge of the grid-stride
// loop, if the tensor size is large, the unroll loop will kick in and the float4
// from curand4 will start getting utilized (for common tensor sizes, we end up
// using rand.x from each thread). Hence, the philox_offset is
// (number of elements per thread * number of engine calls), which makes
// sure that philox offset increment is not less than the number of randoms used
// in each thread.
std::tuple<uint64_t, dim3, dim3> calc_execution_policy(int64_t total_elements) {
  const uint64_t numel = static_cast<uint64_t>(total_elements);
  const uint32_t block_size = block_size_bound;
  const uint32_t unroll = curand4_engine_calls;
  dim3 dim_block(block_size);
  dim3 grid((numel + block_size - 1) / block_size);
  uint32_t blocks_per_sm = at::cuda::getCurrentDeviceProperties()->maxThreadsPerMultiProcessor / block_size;
  grid.x = std::min(
      static_cast<uint32_t>(at::cuda::getCurrentDeviceProperties()->multiProcessorCount) * blocks_per_sm,
      grid.x);
  //number of times random will be generated per thread, to offset philox counter in thc random state
  uint64_t counter_offset = ((numel - 1) / (block_size * grid.x * unroll) + 1)
                                * curand4_engine_calls;
  return std::make_tuple(counter_offset, grid, dim_block);
}

// grid stride loop kernel for distributions
template<typename accscalar_t, int unroll_factor, typename dist_t, typename transform_t>
C10_LAUNCH_BOUNDS_2(block_size_bound, grid_size_bound)
__global__ void distribution_elementwise_grid_stride_kernel(int numel,
                                                            std::pair<uint64_t, uint64_t> seeds,
                                                            const dist_t dist_func,
                                                            const transform_t transform_func) {
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  curandStatePhilox4_32_10_t state;
  curand_init(
      seeds.first,
      idx,
      seeds.second,
      &state);
  int rounded_size = ((numel - 1)/(blockDim.x * gridDim.x * unroll_factor)+1) *
      blockDim.x * gridDim.x * unroll_factor;
  for(int linear_index = idx; linear_index < rounded_size; linear_index += blockDim.x * gridDim.x * unroll_factor) {
    auto rand = dist_func(&state);
    #pragma unroll
    for (int ii = 0; ii < unroll_factor; ii++) {
      int li = linear_index + blockDim.x * gridDim.x * ii;
      if (li < numel) {
        transform_func(li, static_cast<accscalar_t>((&rand.x)[ii]));
      }
    }
    __syncthreads();
  }
}

/**
 * distribution_nullary_kernel is analogous to gpu_kernel in
 * ATen/native/cuda/Loops.cuh. Like gpu_kernel, it uses
 * TensorIterator to launch a kernel. However, the differences are
 *   - it launches a grid-stride loop based kernel. The kernel is not
 *     generic like elementwise_kernel in Loops.cuh and is specialized
 *     for the distribution kernels here.
 *   - For big size tensors, we can launch multiple kernels recursively
 *     (i.e. if (!iter.can_use_32bit_indexing())) and hence, the philox
 *     offset calculation is done in this function.
 *
 * FIXME: Can we specialize elementwise_kernel and launch_kernel in Loops.cuh
 * to have grid-stride loop kernel and then use that to launch our distribution
 * kernels? Note that we need a grid-stride loop kernel because, we found by testing
 * that it achieves peak effective bandwidth.
 */
template<typename scalar_t,
         typename accscalar_t,
         int unroll_factor,
         typename RNG,
         typename dist_t,
         typename transform_t>
void distribution_nullary_kernel(at::TensorIterator& iter,
                                 RNG gen,
                                 const dist_t& dist_func,
                                 const transform_t transform_func) {
  static_assert(unroll_factor >= 1, "unroll_factor must be >= 1.");
  int64_t numel = iter.numel();
  if (numel == 0) {
    return;
  }

  auto execution_policy = calc_execution_policy(numel);
  auto counter_offset = std::get<0>(execution_policy);
  auto grid = std::get<1>(execution_policy);
  auto block = std::get<2>(execution_policy);
  std::pair<uint64_t, uint64_t> rng_engine_inputs;
  {
    // See Note [Acquire lock when using random generators]
    std::lock_guard<std::mutex> lock(gen->mutex_);
    rng_engine_inputs = gen->philox_engine_inputs(counter_offset);
  }

  if (!iter.can_use_32bit_indexing()) {
    for (auto& sub_iter : iter.with_32bit_indexing()) {
      distribution_nullary_kernel<scalar_t, accscalar_t, unroll_factor>(sub_iter,
        gen, dist_func, transform_func);
    }
    return;
  }

  char* out_data = (char*)iter.data_ptr(0);

  auto stream = at::cuda::getCurrentCUDAStream();
  if (iter.is_trivial_1d()) {
    auto strides = iter.get_inner_strides();
    int stride0 = strides[0];
    distribution_elementwise_grid_stride_kernel<accscalar_t, unroll_factor><<<grid, block, 0, stream>>>(
      numel,
      rng_engine_inputs,
      dist_func,
      [=]__device__(int idx, accscalar_t rand) {
        scalar_t* out = (scalar_t*)&out_data[stride0 * idx];
        *out = transform_func(rand);
      }
    );
  } else {
    auto offset_calc = make_offset_calculator<1>(iter);
    distribution_elementwise_grid_stride_kernel<accscalar_t, unroll_factor><<<grid, block, 0, stream>>>(
      numel,
      rng_engine_inputs,
      dist_func,
      [=]__device__(int idx, accscalar_t rand) {
        auto offsets = offset_calc.get(idx);
        scalar_t* out = (scalar_t*)&out_data[offsets[0]];
        *out = transform_func(rand);
      }
    );
  }
  AT_CUDA_CHECK(cudaGetLastError());
}
} // namespace

namespace at {
namespace native {
namespace {
// Binary kernel
template <typename func_t, typename inp_offset_calc_t, typename out_offset_calc_t>
__global__ void distribution_binary_elementwise_kernel(
    int numel,
    func_t f,
    std::pair<uint64_t, uint64_t> seeds,
    typename ternary_function_traits<func_t>::result_type *output_data,
    const typename ternary_function_traits<func_t>::arg2_t *input_data_1,
    const typename ternary_function_traits<func_t>::arg3_t *input_data_2,
    inp_offset_calc_t inp_calc,
    out_offset_calc_t out_calc) {
  using input_t_1 = typename ternary_function_traits<func_t>::arg2_t;
  using input_t_2 = typename ternary_function_traits<func_t>::arg3_t;

  input_t_1 inputs_1[THREAD_WORK_SIZE];
  input_t_2 inputs_2[THREAD_WORK_SIZE];

  int base_index = BLOCK_WORK_SIZE * blockIdx.x;
  int remaining = std::min<int>(numel - base_index, BLOCK_WORK_SIZE);

  curandStatePhilox4_32_10_t state;
  curand_init(seeds.first, blockIdx.x * blockDim.x + threadIdx.x, seeds.second, &state);

  // load data into registers
  int thread_idx = threadIdx.x;
  #pragma unroll
  for (int i = 0; i < THREAD_WORK_SIZE; i++) {
    if (thread_idx >= remaining) {
      break;
    }
    int input_idx = thread_idx + base_index;
    auto offsets = inp_calc.get(input_idx);
    inputs_1[i] = input_data_1[offsets[0]];
    inputs_2[i] = input_data_2[offsets[1]];

    thread_idx += num_threads;
  }

  // compute and store
  thread_idx = threadIdx.x;
  #pragma unroll
  for (int i = 0; i < THREAD_WORK_SIZE; i++) {
    if (thread_idx >= remaining) {
      break;
    }
    int input_idx = thread_idx + base_index;
    auto offsets = out_calc.get(input_idx);
    output_data[offsets[0]] = f(state, inputs_1[i], inputs_2[i]);
    thread_idx += num_threads;
  }
}

template <typename func_t>
void distribution_binary_kernel(TensorIterator &iter, std::pair<uint64_t, uint64_t> seeds, const func_t &f) {
  using traits = ternary_function_traits<func_t>;
  static_assert(std::is_same<typename traits::arg1_t, curandStatePhilox4_32_10_t &>::value, "the first argument of functor must be curandStatePhilox4_32_10_t");
  using input_t_1 = typename traits::arg2_t;
  using input_t_2 = typename traits::arg3_t;
  using output_t = typename traits:result_type;

  if (!iter.can_use_32bit_indexing()) {
    for (auto& sub_iter : iter.with_32bit_indexing()) {
      distribution_binary_kernel(sub_iter, seeds, f);
    }
    return;
  }

  TORCH_INTERNAL_ASSERT_DEBUG_ONLY(iter.can_use_32bit_indexing());

  uint64_t numel = iter.numel();
  if (numel == 0) {
    return;
  }

  output_t *output_data = static_cast<output_t *>(iter.data_ptr(0));
  const input_t_1 *input_data_1 = static_cast<const input_t_1 *>(iter.data_ptr(1));
  const input_t_2 *input_data_2 = static_cast<const input_t_2 *>(iter.data_ptr(2));

  uint64_t grid = (numel + block_work_size - 1) / block_work_size;
  auto stream = at::cuda::getCurrentCUDAStream();

  if (iter.is_contiguous()) {
    distribution_binary_elementwise_kernel<<<grid,num_threads, 0, stream>>>(
        numel, f, seeds, output_data, input_data_1, input_data_2,
        TrivialOffsetCalculator<2>(), TrivialOffsetCalculator<1>());
  } else {
    std::array<const uint64_t*, 1> out_strides = {iter.strides(0).data()};
    std::array<const uint64_t*, 1> inp_strides_1 = {iter.strides(1).data()};
    std::array<const uint64_t*, 1> inp_strides_2 = {iter.strides(2).data()};

    distribution_binary_elementwise_kernel<<<grid, num_threads, 0, stream>>>(
        numel, f, seeds, output_data, input_data_1, input_data_2,
        make_input_offset_calculator<2>(iter), make_output_offset_calculator(iter));
  }
}

} // namespace
}} // namespace at::native


namespace at {
namespace native {
namespace templates {
namespace cuda {

// ==================================================== Random ========================================================

template<typename RNG>
void random_from_to_kernel(TensorIterator& iter, uint64_t range, int64_t base, RNG gen) {
#ifdef _WIN32
  // TODO: https://github.com/pytorch/pytorch/issues/33793
  if (iter.dtype() == ScalarType::BFloat16) {
    TORCH_CHECK(false, "random_() is not supported for bfloat16 CUDA tensors on Windows. Please see https://github.com/pytorch/pytorch/issues/33793");
  }
#endif
  AT_DISPATCH_ALL_TYPES_AND3(at::ScalarType::Bool, at::ScalarType::Half, at::ScalarType::BFloat16, iter.dtype(), "random_from_to_kernel_cuda", [&] {
    if ((
      std::is_same<scalar_t, int64_t>::value ||
      std::is_same<scalar_t, double>::value ||
      std::is_same<scalar_t, float>::value ||
      std::is_same<scalar_t, at::BFloat16>::value) && range >= 1ULL << 32)
    {
      // define lambda to mod with range and add base
      auto random_func = [range, base] __device__ (uint64_t rand) {
        return static_cast<scalar_t>(static_cast<int64_t>(rand % range + base));
      };
      distribution_nullary_kernel<scalar_t, uint64_t, curand4_engine_calls/2>(iter,
        gen,
        [] __device__ (curandStatePhilox4_32_10_t* state) -> ulonglong2 {
          ulonglong2 ret;
          uint4 rand_val = curand4(state);
          ret.x = (static_cast<uint64_t>(rand_val.x) << 32) | rand_val.y;
          ret.y = (static_cast<uint64_t>(rand_val.z) << 32) | rand_val.w;
          return ret;
        },
        random_func);
    } else {
      auto random_func = [range, base] __device__ (uint32_t rand) {
        return static_cast<scalar_t>(static_cast<int64_t>(rand % range + base));
      };
      distribution_nullary_kernel<scalar_t, uint32_t, curand4_engine_calls>(iter,
        gen,
        [] __device__ (curandStatePhilox4_32_10_t* state) {
          return curand4(state);
        },
        random_func);
    }
   });
}

// This is the special kernel to handle single specific case:
// from(inclusive) = std::numeric_limits<int64_t>::lowest()
// to(exclusive) = None (= std::numeric_limits<int64_t>::max() + 1)
template<typename RNG>
void random_full_64_bits_range_kernel(TensorIterator& iter, RNG gen) {
#ifdef _WIN32
  // TODO: https://github.com/pytorch/pytorch/issues/33793
  if (iter.dtype() == ScalarType::BFloat16) {
    TORCH_CHECK(false, "random_() is not supported for bfloat16 CUDA tensors on Windows. Please see https://github.com/pytorch/pytorch/issues/33793");
  }
#endif
  AT_DISPATCH_ALL_TYPES_AND(at::ScalarType::BFloat16, iter.dtype(), "random_full_64_bits_range_kernel_cuda", [&] {
    if (std::is_same<scalar_t, int64_t>::value ||
        std::is_same<scalar_t, double>::value ||
        std::is_same<scalar_t, float>::value ||
        std::is_same<scalar_t, at::BFloat16>::value) {
      auto random_func = [] __device__ (uint64_t rand) {
        return static_cast<scalar_t>(static_cast<int64_t>(rand));
      };
      distribution_nullary_kernel<scalar_t, uint64_t, curand4_engine_calls/2>(iter,
        gen,
        [] __device__ (curandStatePhilox4_32_10_t* state) -> ulonglong2 {
          ulonglong2 ret;
          uint4 rand_val = curand4(state);
          ret.x = (static_cast<uint64_t>(rand_val.x) << 32) | rand_val.y;
          ret.y = (static_cast<uint64_t>(rand_val.z) << 32) | rand_val.w;
          return ret;
        },
        random_func);
    } else {
      TORCH_CHECK(false, "random_full_64_bits_range_kernel_cuda handles only int64, double, float and bfloat16");
    }
  });
}

template<typename RNG>
struct RandomFromToKernel {
  void operator()(TensorIterator& iter, uint64_t range, int64_t base, c10::optional<Generator> gen) {
    random_from_to_kernel(iter, range, base, check_generator<RNG>(gen));
  }
  void operator()(TensorIterator& iter, c10::optional<Generator> gen) {
    random_full_64_bits_range_kernel(iter, check_generator<RNG>(gen));
  }
};

template<typename RNG>
void random_kernel(TensorIterator& iter, RNG gen) {
#ifdef _WIN32
  // TODO: https://github.com/pytorch/pytorch/issues/33793
  if (iter.dtype() == ScalarType::BFloat16) {
    TORCH_CHECK(false, "random_() is not supported for bfloat16 CUDA tensors on Windows. Please see https://github.com/pytorch/pytorch/issues/33793");
  }
#endif
  if (isFloatingType(iter.dtype())) {
    AT_DISPATCH_FLOATING_TYPES_AND2(at::ScalarType::Half, at::ScalarType::BFloat16, iter.dtype(), "random_kernel_fp_cuda", [&] {
      if (std::is_same<scalar_t, double>::value) {
        auto random_func = [] __device__ (uint64_t rand) {
          return static_cast<scalar_t>(rand % static_cast<uint64_t>((1ULL << std::numeric_limits<scalar_t>::digits) + 1));
        };
        distribution_nullary_kernel<scalar_t, uint64_t, curand4_engine_calls/2>(iter,
          gen,
          [] __device__ (curandStatePhilox4_32_10_t* state) -> ulonglong2 {
            ulonglong2 ret;
            uint4 rand_val = curand4(state);
            ret.x = (static_cast<uint64_t>(rand_val.x) << 32) | rand_val.y;
            ret.y = (static_cast<uint64_t>(rand_val.z) << 32) | rand_val.w;
            return ret;
          },
          random_func);
      } else {
        auto random_func = [] __device__ (uint32_t rand) {
          return static_cast<scalar_t>(rand % static_cast<uint64_t>((1ULL << std::numeric_limits<scalar_t>::digits) + 1));
        };
        distribution_nullary_kernel<scalar_t, uint32_t, curand4_engine_calls>(iter,
          gen,
          [] __device__ (curandStatePhilox4_32_10_t* state) {
            return curand4(state);
          },
          random_func);
      }
    });
  } else if (isIntegralType(iter.dtype(), /*includeBool=*/true)) {
    AT_DISPATCH_INTEGRAL_TYPES_AND(at::ScalarType::Bool, iter.dtype(), "random_kernel_int_cuda", [&] {
      if (std::is_same<scalar_t, int64_t>::value) {
        auto random_func = [] __device__ (uint64_t rand) {
          return static_cast<scalar_t>(rand % (static_cast<uint64_t>(std::numeric_limits<scalar_t>::max()) + 1));
        };
        distribution_nullary_kernel<scalar_t, uint64_t, curand4_engine_calls/2>(iter,
          gen,
          [] __device__ (curandStatePhilox4_32_10_t* state) -> ulonglong2 {
            ulonglong2 ret;
            uint4 rand_val = curand4(state);
            ret.x = (static_cast<uint64_t>(rand_val.x) << 32) | rand_val.y;
            ret.y = (static_cast<uint64_t>(rand_val.z) << 32) | rand_val.w;
            return ret;
          },
          random_func);
      } else if (std::is_same<scalar_t, bool>::value) {
        auto random_func = [] __device__ (uint32_t rand) {
          return static_cast<scalar_t>(rand & 1);
        };
        distribution_nullary_kernel<scalar_t, uint32_t, curand4_engine_calls>(iter,
          gen,
          [] __device__ (curandStatePhilox4_32_10_t* state) {
            return curand4(state);
          },
          random_func);
      } else {
        auto random_func = [] __device__ (uint32_t rand) {
          return static_cast<scalar_t>(rand % (static_cast<uint64_t>(std::numeric_limits<scalar_t>::max()) + 1));
        };
        distribution_nullary_kernel<scalar_t, uint32_t, curand4_engine_calls>(iter,
          gen,
          [] __device__ (curandStatePhilox4_32_10_t* state) {
            return curand4(state);
          },
          random_func);
      }
    });
  } else {
    TORCH_CHECK(false, "random_kernel_cuda handles only integral, floating-point and boolean types");
  }
}

template<typename RNG>
struct RandomKernel {
  void operator()(TensorIterator& iter, RNG gen) {
    random_kernel(iter, gen);
  }
};

// ==================================================== Normal ========================================================

template<typename RNG>
void normal_kernel(Tensor& self, double mean_, double std_, RNG gen) {
  auto iter = TensorIterator::nullary_op(self);
  AT_DISPATCH_FLOATING_TYPES_AND2(at::ScalarType::Half, at::ScalarType::BFloat16, iter.dtype(), "normal_kernel_cuda", [&] {
    using accscalar_t = at::acc_type<scalar_t, true>;
    auto mean = static_cast<accscalar_t>(mean_);
    auto std = static_cast<accscalar_t>(std_);
    // define lambda to multiply std and add mean
    auto normal_func = [mean, std] __device__ (accscalar_t rand) {
      return static_cast<scalar_t>(rand * std + mean);
    };
    if (std::is_same<scalar_t, double>::value) {
      distribution_nullary_kernel<scalar_t, accscalar_t, curand4_engine_calls/2>(iter,
        gen,
        [] __device__ (curandStatePhilox4_32_10_t* state) { return curand_normal2_double(state); },
        normal_func);
    } else {
      distribution_nullary_kernel<scalar_t, accscalar_t, curand4_engine_calls>(iter,
        gen,
        [] __device__ (curandStatePhilox4_32_10_t* state) { return curand_normal4(state); },
        normal_func);
    }
   });
}

template<typename RNG>
struct NormalKernel {
  void operator()(Tensor& self, double mean, double std, c10::optional<Generator> gen) {
    normal_kernel(self, mean, std, check_generator<RNG>(gen));
  }
};

// ==================================================== Uniform ========================================================

template<typename RNG>
void uniform_kernel(TensorIterator& iter, double from_, double to_, RNG gen) {
#ifdef _WIN32
  // TODO: https://github.com/pytorch/pytorch/issues/33793
  if (iter.dtype() == ScalarType::BFloat16) {
    TORCH_CHECK(false, "uniform_() is not supported for bfloat16 CUDA tensors on Windows. Please see https://github.com/pytorch/pytorch/issues/33793");
  }
#endif
  AT_DISPATCH_FLOATING_TYPES_AND2(at::ScalarType::Half, at::ScalarType::BFloat16, iter.dtype(), "uniform_kernel_cuda", [&] {
    auto from = static_cast<scalar_t>(from_);
    auto to = static_cast<scalar_t>(to_);
    using accscalar_t = at::acc_type<scalar_t, true>;
    auto range = static_cast<accscalar_t>(to-from);
    from = static_cast<accscalar_t>(from);
    // define lambda to reverse bounds, multiply 'range' and add 'from_'
    auto uniform_func = [range, from] __device__ (accscalar_t rand) {
      // reverse the bounds of curand4 from (0, 1] to [0, 1)
      // Note that this method is from legacy THCTensorRandom and is likely to give
      // you more 0-s, since, the probability of gettings 1-s is higher than 0-s and
      // by reversing the bounds, we are flipping the probabilities of 1-s and 0-s.
      auto reverse_bound_rand = rand == static_cast<accscalar_t>(1.0) ? static_cast<accscalar_t>(0.0) : rand;
      return static_cast<scalar_t>(reverse_bound_rand * range + from);
    };
    if (std::is_same<scalar_t, double>::value) {
      distribution_nullary_kernel<scalar_t, accscalar_t, curand4_engine_calls/2>(iter,
        gen,
        [] __device__ (curandStatePhilox4_32_10_t* state) { return curand_uniform2_double(state); },
        uniform_func);
    } else {
      distribution_nullary_kernel<scalar_t, accscalar_t, curand4_engine_calls>(iter,
        gen,
        [] __device__ (curandStatePhilox4_32_10_t* state) { return curand_uniform4(state); },
        uniform_func);
    }
   });
}

template<typename RNG>
struct UniformKernel {
  void operator()(TensorIterator& iter, double from, double to, c10::optional<Generator> gen) {
    uniform_kernel(iter, from, to, check_generator<RNG>(gen));
  }
};

}}}}
