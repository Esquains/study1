#include <ATen/ATen.h>
#include <ATen/InitialTensorOptions.h>
#include <ATen/NativeFunctions.h>
#include <ATen/cuda/CUDAApplyUtils.cuh>
#include <ATen/cuda/CUDAContext.h>
#include <ATen/native/TensorFactories.h>
#include <ATen/native/cuda/Resize.cuh>
#include <c10/util/Exception.h>

#include <THC/THCGeneral.h>
#include <THC/THCThrustAllocator.cuh>
#include <THC/THCTensorRandom.h>
#include <thrust/device_ptr.h>
#include <thrust/sort.h>
#include <thrust/execution_policy.h>
#include <thrust/sequence.h>
#include <thrust/binary_search.h>

#include <algorithm>
#include <cstddef>
#include <cmath>

THCGenerator* THCRandom_getGenerator(THCState* state);
// This is to keep thread safety in curand with curandStateMtgp32
int const threadsPerBlock = 256;

namespace at {
namespace native {

Tensor& eye_out_cuda(Tensor& result, int64_t n) {
  return at::native::eye_out_cuda(result, n, /*m=*/-1);
}

Tensor& eye_out_cuda(Tensor& result, int64_t n, int64_t m) {
  TORCH_CHECK(n >= 0, "n must be greater or equal to 0, got ", n);

  if(m < 0) {
    m = n;
  }

  result.resize_({n, m});
  result.zero_();

  int64_t sz = std::min<int64_t>(n, m);
  int64_t stride = result.stride(0) + result.stride(1);

  Tensor diag = result.as_strided({sz}, {stride});
  diag.fill_(1);
  return result;
}

Tensor empty_cuda(IntArrayRef size, const TensorOptions& options) {
  AT_ASSERT(options.backend() == at::Backend::CUDA);
  AT_ASSERT(!options.is_variable());  // is_variable should have been 'unpacked'  // TODO: remove this when Variable and Tensor are merged
  TORCH_CHECK(!options.pinned_memory(), "Only dense CPU tensors can be pinned");
  check_size_nonnegative(size);

  auto* allocator = at::cuda::getCUDADeviceAllocator();
  int64_t nelements = prod_intlist(size);
  auto dtype = options.dtype();
  auto storage_impl = c10::make_intrusive<StorageImpl>(
    dtype,
    nelements,
    allocator->allocate(nelements * dtype.itemsize()),
    allocator,
    /*resizeable=*/true);

  auto tensor = detail::make_tensor<TensorImpl>(storage_impl, CUDATensorId());
  // Default TensorImpl has size [0]
  if (size.size() != 1 || size[0] != 0) {
    tensor.unsafeGetTensorImpl()->set_sizes_contiguous(size);
  }
  return tensor;
}

Tensor empty_strided_cuda(IntArrayRef size, IntArrayRef stride, const TensorOptions& options) {
  auto t = at::native::empty_cuda({0}, options);
  at::native::resize_impl_cuda_(t.unsafeGetTensorImpl(), size, stride);
  return t;
}

Tensor& randperm_out_cuda(Tensor& result, int64_t n, Generator* generator) {
  TORCH_CHECK(n >= 0, "n must be non-negative, got", n);
  TORCH_CHECK(at::scalar_tensor(n, result.options()).defined(),
  "n is too large for result tensor type: '", result.type().toString(), "'");

  result.resize_({n});

  if (result.scalar_type() == at::ScalarType::Half) {
    auto result_float = at::empty({n}, initialTensorOptions().device(Device(DeviceType::CUDA)));
    result.copy_(randperm_out_cuda(result_float, n, generator));
  } else {
    if (n < 30000) {  // For small inputs, we offload it to CPU instead.
      auto result_cpu = at::empty({n}, result.options().device(kCPU));
      randperm_out(result_cpu, n, generator);
      result.copy_(result_cpu);
    } else {
      // Generate random values for the keys array
      AT_DISPATCH_ALL_TYPES(
        result.scalar_type(), "randperm_out_cuda", [&] {
          auto keys = at::empty(result.sizes(), result.options()).random_(generator);

          auto result_data = thrust::device_ptr<scalar_t>(result.data<scalar_t>());
          auto keys_data = thrust::device_ptr<scalar_t>(keys.data<scalar_t>());

          auto state = globalContext().getTHCState();
          THCThrustAllocator thrustAlloc(state);
          auto policy = thrust::cuda::par(thrustAlloc).on(at::cuda::getCurrentCUDAStream());

          thrust::sequence(policy, result_data, result_data + n);

          // Use the sorted order of keys to rearrange the result array
          thrust::sort_by_key(policy, keys_data, keys_data + n, result_data);
        }
      );
    }
  }

  return result;
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ triangle ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

namespace {
// To find the max integer that does not exceed the root of an int64_t variable,
// we could use a loop to test one bit at a time, which takes up to 31
// iterations. This would give the accurate result, but is relatively slow and
// is an overkill for most cases where double's precision suffice.
//
// If we directly use sqrt to calculate the root, the convertion from int64_t
// to double would lose 11 bits precision.
//
// The following solution uses sqrt directly for most cases, and would only
// special handle it if there is indeed precision loss.
__device__
inline int64_t resolve_root_int(
    int64_t b, int64_t cX4, int64_t x, int32_t sign) {
  int64_t bXb_cX4 = b*b - cX4;
  // potential precision loss could occur here when casting int64_t (63 bits
  // precision) to double (52 bits precision)
  double sr = ::sqrt((double)bXb_cX4);
  int64_t res = ::__double2ll_rd((-b + sign * sr)/2);

  // have to cast double to int64_t, otherwise it would only compare up to the
  // precision of a double variable, ignoring the precision loss
  if (bXb_cX4 != (int64_t) (sr * sr)) {
    // handle precision loss by using binary search
    int64_t llsr = ::__double2ll_rd(sr);
    // Use the following math to reduce search space.
    // Suppose z is the accurate result of sqrt(bXb_cX4) without precision loss
    // let d = abs(bXb_cX4 - llsr * llsr), then we have:
    // z = sqrt(bXb_cX4) <= sqrt(llsr * llsr + d) <= llsr + sqrt(d)
    // z = sqrt(bXb_cX4) >= sqrt(llsr * llsr - d) >= llsr - sqrt(d)
    // Hence, it is sufficient to search range [llsr - sqrt(d), llsr + sqrt(d)).
    // And the true value of row would also be with in range,
    //            [res - sqrt(d), res + sqrt(d) + 1)
    // as the denominator would only reduce the precision penalty.
    int64_t diff =
      ::__double2ll_ru(::sqrt(::fabs((double)(bXb_cX4 - llsr * llsr))));
    // l never exceeds (could equal to) the target row index
    auto l = res > diff ? res - diff : 0;
    // r is always larger than the target row index
    auto r = res + diff + 1;

    // binary search for the correct answer
    x <<= 1; // the loop always compares with 2x, so do it once here
    while (l + 1 < r) {
      auto m = (l + r) >> 1;
      // for tril:
      //    b = 2f - 1, sign = 1, hence (2f + m - 1) * m / 2
      // for triu:
      //    b = -2f - 1, sign = -1, hence (2f - m + 1) * m / 2
      if (sign * (b + m) * m > x) {
        r = m;
      } else {
        l = m;
      }
    }
    res = l;
  }

  return res;
}

// f: the number of elements in the first row of the trapezoid.
// x: the index of the target coordinates ordered by row and then column.
//
// View the tril as a top trapezoid stacked on a bottom rectangle. Assume x
// corresponds to the coordinate (row, col) in the trapezoid, where the row and
// the col both start from 0, then we have:
//
//                   (f + f + row - 1) * row / 2 <= x                       [1]
//                 (f + f + row) * (row + 1) / 2  > x                       [2]
//
// Therefore, row is the maximum integer satisfying the following inequality:
//
//                       (row + 2f - 1)row <= 2x
//                  row^2 + (2f-1)row - 2x <= 0.                            [3]
//
// Based on ineuqality [3], we have the following coefficients for formula of
// root:
//                               a = 1
//                               b = 2f - 1
//                               c = -2x
// There are two roots, and we should use the largest integer that does not
// exceed the root on the right. Intuitively, it is because:
//  i)  the valid solution range of row is between two roots, as it is <= 0;
//  ii) as we count in more rows, the total # of elements should always
//      increase, hence so does the left-hand side row^2 + (2f-1)row - 2x.
//      Therefore, the valid range of row lies in between the nadir point and
//      the larger root on the right.
// Full proof can be derived from inequality [2]. So, we calculate the result
// coordinate as:
//
//                   row = floor((-b + sqrt(b^2 - 4c)) / 2)
//                   col = x - (f + f + row - 1) * row / 2
__device__
inline void get_coordinate_in_tril_trapezoid(
    int64_t f, int64_t x, int64_t & row, int64_t & col) {
  f <<= 1; // all statements use 2f, so only calculate it once here.
  auto b = f - 1;
  auto cX4 = - (x << 3); // 4 * c = 4 * (-2x) = -8x;
  row = resolve_root_int(b, cX4, x, 1);
  col = x - ((f + row - 1) * row >> 1);
}

// f: the number of elements in the first row of the bottom trapezoid.
// x: the index of the target coordinates ordered by row and then column.
//
// View the triu as a top rectangle stacked on a bottom trapezoid, where the
// trapezoid is upside down. Assume x corresponds to the coordinate (row, col)
// in the bottom trapezoid, where the row and the col start from 0, then we
// have:
//
//                   (f + f - row + 1) * row / 2 <= x                       [1]
//                 (f + f - row) * (row + 1) / 2  > x                       [2]
//
// Therefore, row is the maximum integer satisfying the following inequality:
//
//                       (-row + 2f + 1)row <= 2x
//                   row^2 - (2f+1)row + 2x >= 0.                           [3]
//
// Based on ineuqality [3], we have the following coefficients for formula of
// root:
//                               a = 1
//                               b = -1 - 2f
//                               c = 2x
// There are two roots, and we should use the largest integer that does not
// exceed the root on the left. Intuitively, it is because:
//  i)  the valid solution range of row is outside of the two roots, as it is <
//      > 0;
//  ii) as we count in more rows, the total # of elements should always
//      increase, hence so does the left-hand side row^2 - (2f+1)row + 2x.
//      Therefore, the valid range of row lies to the left of the smaller root
//      on the left.
// Full proof can be derived from inequality [2]. So, we calculate the result
// coordinate as:
//
//                   row = floor((-b - sqrt(b^2 - 4c)) / 2)
//                   col = x - (f + f - row + 1) * row / 2
__device__
inline void get_coordinate_in_triu_trapezoid(
    int64_t f, int64_t x, int64_t & row, int64_t & col) {
  f <<= 1; // all statements use 2f, so only calculate it once here.
  auto b = -1 - f;
  auto cX4 = x << 3; // 4 * c = 4 * (2x) = 8x;
  row = resolve_root_int(b, cX4, x, -1);
  col = x - ((f - row + 1) * row >> 1) + row;
}

} // namespace

template <typename scalar_t>
__global__
#ifdef __HIP_PLATFORM_HCC__
C10_LAUNCH_BOUNDS_1(512)
#endif
void tril_indices_kernel(scalar_t * tensor,
                         int64_t row_offset,
                         int64_t m_first_row,
                         int64_t col,
                         int64_t trapezoid_size,
                         int64_t tril_size) {
  int64_t linear_index = blockIdx.x * blockDim.x + threadIdx.x;

  if (linear_index < tril_size) {
    int64_t r, c;
    if (linear_index < trapezoid_size) {
      // the coordinate is within the top trapezoid
      get_coordinate_in_tril_trapezoid(m_first_row, linear_index, r, c);
    } else {
      // the coordinate falls in the bottom rectangle
      auto surplus = linear_index - trapezoid_size;
      // add the height of trapezoid: m_last_row (col) - m_first_row + 1
      r = surplus / col + col - m_first_row + 1;
      c = surplus % col;
    }
    r += row_offset;

    tensor[linear_index] = r;
    tensor[linear_index + tril_size] = c;
  }
}

// Some Large test cases for the fallback binary search path is disabled by
// default to speed up CI tests and to avoid OOM error. When modifying the
// implementation, please enable them in test/test_cuda.py and make sure they
// pass on your local server.
Tensor tril_indices_cuda(
    int64_t row, int64_t col, int64_t offset, const TensorOptions& options) {
  check_args(row, col, options);

  auto tril_size = get_tril_size(row, col, offset);
  auto tensor = empty_cuda({2, tril_size}, options);

  if (tril_size > 0) {
    auto m_first_row = offset > 0 ?
      std::min<int64_t>(col, 1 + offset) : // upper bounded by col
      row + offset > 0; // either 0 or 1
    auto trapezoid_row_offset = std::max<int64_t>(0, -offset);
    auto rectangle_row_offset = trapezoid_row_offset + col - m_first_row + 1;
    int64_t rectangle_size = 0;
    if (rectangle_row_offset < row) {
      rectangle_size = (row - rectangle_row_offset) * col;
    }

    dim3 dim_block = cuda::getApplyBlock();
    dim3 dim_grid;
    // using tril_size instead of tensor.numel(), as each thread takes care of
    // two elements in the tensor.
    TORCH_CHECK(
      cuda::getApplyGrid(tril_size, dim_grid, tensor.get_device()),
      "unable to get dim grid");

    AT_DISPATCH_ALL_TYPES_AND(at::ScalarType::Half, tensor.scalar_type(), "tril_indices_cuda", [&] {
      tril_indices_kernel<<<
          dim_grid, dim_block, 0, at::cuda::getCurrentCUDAStream()>>>(
        tensor.data<scalar_t>(),
        trapezoid_row_offset,
        m_first_row,
        col,
        tril_size - rectangle_size,
        tril_size);
    });
  }

  return tensor;
}

template <typename scalar_t>
__global__
void triu_indices_kernel(scalar_t * tensor,
                         int64_t col_offset,
                         int64_t m_first_row,
                         int64_t col,
                         int64_t rectangle_size,
                         int64_t triu_size) {
  int64_t linear_index = blockIdx.x * blockDim.x + threadIdx.x;

  if (linear_index < triu_size) {
    int64_t r, c;
    if (linear_index < rectangle_size) {
      // the coordinate is within the top rectangle
      r = linear_index / col;
      c = linear_index % col;
    } else {
      // the coordinate falls in the bottom trapezoid
      get_coordinate_in_triu_trapezoid(
        m_first_row, linear_index - rectangle_size, r, c);
      r += rectangle_size / col;
    }

    c += col_offset;
    tensor[linear_index] = r;
    tensor[linear_index + triu_size] = c;
  }
}

// Some Large test cases for the fallback binary search path is disabled by
// default to speed up CI tests and to avoid OOM error. When modifying the
// implementation, please enable them in test/test_cuda.py and make sure they
// pass on your local server.
Tensor triu_indices_cuda(
    int64_t row, int64_t col, int64_t offset, const TensorOptions& options) {
  check_args(row, col, options);

  auto triu_size = row * col - get_tril_size(row, col, offset - 1);
  auto tensor = empty_cuda({2, triu_size}, options);

  if (triu_size > 0) {
    // # of triu elements in the first row
    auto m_first_row = offset > 0 ?
      std::max<int64_t>(col - offset, 0) : // upper bounded by col
      col;

    // size of the top rectangle
    int64_t rectangle_size = 0;
    if (offset < 0) {
      rectangle_size = std::min<int64_t>(row, -offset) * col;
    }

    dim3 dim_block = cuda::getApplyBlock();
    dim3 dim_grid;

    // using triu_size instead of tensor.numel(), as each thread takes care of
    // two elements in the tensor.
    TORCH_CHECK(
      cuda::getApplyGrid(triu_size, dim_grid, tensor.get_device()),
      "unable to get dim grid");

    AT_DISPATCH_ALL_TYPES_AND(at::ScalarType::Half, tensor.scalar_type(), "triu_indices_cuda", [&] {
      triu_indices_kernel<<<
          dim_grid, dim_block, 0, at::cuda::getCurrentCUDAStream()>>>(
        tensor.data<scalar_t>(),
        std::max<int64_t>(0, offset),
        m_first_row,
        col,
        rectangle_size,
        triu_size);
    });
  }

  return tensor;
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ choice ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

__global__ void generate_samples(
  int64_t *samples,
  int64_t k,
  int64_t n,
  std::pair<uint64_t, uint64_t> seeds
){
  int thread_id = blockIdx.x * blockDim.x + threadIdx.x;
  curandStatePhilox4_32_10_t state;
  curand_init(seeds.first, thread_id, seeds.second, &state);
  int64_t s = curand4(&state).x % (thread_id + k + 1);
  if (thread_id < n){
    samples[thread_id] = s;
  }
}

__global__ void generate_keys(
  float *keys,
  float *weights,
  int64_t n,
  std::pair<uint64_t, uint64_t> seeds
){
  int thread_id = blockIdx.x * blockDim.x + threadIdx.x;
  curandStatePhilox4_32_10_t state;
  curand_init(seeds.first, thread_id, seeds.second, &state);
  float u = curand_uniform4(&state).x;
  if(thread_id < n){
    keys[thread_id] = weights[thread_id] > 0 ? (float) __powf(u, (float) 1 / weights[thread_id]):-1;
  }
}

__global__ void sampling_with_replacement_kernel(
  int64_t *samples,
  float *cdf,
  int64_t n,
  int64_t k,
  std::pair<uint64_t, uint64_t> seeds
){
  int thread_id = blockIdx.x * blockDim.x + threadIdx.x;
  curandStatePhilox4_32_10_t state;
  curand_init(seeds.first, thread_id, seeds.second, &state);
  float u = curand_uniform4(&state).x;
  if(thread_id < k){
    auto ptr = thrust::lower_bound(thrust::device, cdf, cdf + n, u);
    samples[thread_id] = thrust::distance(cdf, ptr);
  }
}

__global__ void generate_reservoir(
  int64_t *indices,
  int64_t *samples,
  int64_t nb_iterations,
  int64_t k
){
  for(int i = 0; i < nb_iterations; i++){
    int64_t z = samples[i];
    if (z < k) {
      thrust::swap(indices[z], indices[i + k]);
    }
  }
}

Tensor reservoir_sampling_cuda(
  const Tensor& x,
  const Tensor& weights,
  int64_t k
){

  TORCH_CHECK(
    x.dim() > 0,
    "The input Tensor must have at least one dimension"
  );

  int n = x.size(0);

  TORCH_CHECK(
    n >= k,
    "Cannot take a larger sample than population when 'replace=False'"
  );

  auto options = x.options().dtype(at::kLong);
  dim3 threads(threadsPerBlock);

  THCState *state = at::globalContext().getTHCState();
  THCGenerator* gen = THCRandom_getGenerator(state);
  uint64_t offset = gen->state.philox_seed_offset.fetch_add(4);
  std::pair<uint64_t, uint64_t> next_philox_seed = std::make_pair(
                                                    gen->state.initial_seed,
                                                    offset
                                                  );

  if (weights.numel() == 0){ // Uniform Sampling
    Tensor indices_n = at::arange({n}, options);

    // This is a trick to speed up the reservoir sampling.
    // It makes the worst case be k = n / 2.
    int split, begin, end;
    if(2 * k < n){
      split = n - k;
      begin = n - k;
      end = n;
    } else {
      split = k;
      begin = 0;
      end = k;
    }

    int nb_iterations = std::min(k, n - k);
    dim3 blocks((nb_iterations + threadsPerBlock - 1)/threadsPerBlock);

    Tensor samples = at::arange({nb_iterations}, options);

    generate_samples<<<blocks, threads>>>(
      samples.data<int64_t>(),
      split,
      n,
      next_philox_seed
    );

    AT_CUDA_CHECK(cudaGetLastError());

    // This must be done in a separeted kernel
    // since this algorithm isn't thread safe
    generate_reservoir<<<1, 1>>>(
      indices_n.data<int64_t>(),
      samples.data<int64_t>(),
      nb_iterations,
      split
    );

    AT_CUDA_CHECK(cudaGetLastError());

    return x.index_select(
      0,
      indices_n.index_select(
        0,
        at::arange(begin, end, options)
      )
    );

  } else { // Weighted Sampling

    // If the weights are contiguous floating points, then
    // the next step won't generate a copy.
    Tensor weights_contiguous = weights.contiguous().to(at::kFloat);

    TORCH_CHECK(
      weights_contiguous.device() == x.device(),
      "The weights must share the same device as the inputs."
    );

    TORCH_CHECK(
      n == weights_contiguous.numel(),
      "The weights must have the same number of elements as the input's first dimension."
    );

    TORCH_CHECK(
      weights_contiguous.dim() == 1,
      "The weights must 1-dimensional."
    );

    TORCH_CHECK(
      weights_contiguous.nonzero().numel() >= k,
      "Cannot have less non-zero weights than the number of samples."
    );

    TORCH_CHECK(
      weights_contiguous.min().item().toLong() >= 0,
      "All the weights must be non-negative."
    );

    Tensor keys = at::empty({n}, weights_contiguous.options());
    dim3 all_blocks((n + threadsPerBlock - 1)/threadsPerBlock);

    generate_keys<<<all_blocks, threads>>>(
      keys.data<float>(),
      weights_contiguous.data<float>(),
      n,
      next_philox_seed
    );

    AT_CUDA_CHECK(cudaGetLastError());

    return x.index_select(0, std::get<1>(keys.topk(k)));
  }
}

Tensor sampling_with_replacement_cuda(
  const Tensor& x,
  const Tensor& weights,
  int64_t k
){

  TORCH_CHECK(
    x.dim() > 0,
    "The input Tensor must have at least one dimension"
  );

  int n = x.size(0);
  Tensor samples;

  if (weights.numel() == 0){ // Uniform Sampling
    samples = at::randint(0, n, {k}, x.options().dtype(at::kLong));
  } else { // Weighted Sampling

    TORCH_CHECK(
      weights.min().item().toLong() >= 0,
      "All the weights must be non-negative."
    );


    TORCH_CHECK(
	    n == weights.numel(),
	    "The weights must have the same number of elements as the input's first dimension."
	  );

	  TORCH_CHECK(
	    weights.dim() == 1,
	    "The weights must 1-dimensional."
	  );

    THCState *state = at::globalContext().getTHCState();
    THCGenerator* gen = THCRandom_getGenerator(state);
    uint64_t offset = gen->state.philox_seed_offset.fetch_add(4);
    std::pair<uint64_t, uint64_t> next_philox_seed = std::make_pair(
                                                      gen->state.initial_seed,
                                                      offset
                                                    );


    samples = at::empty({k}, x.options().dtype(at::kLong));
    Tensor cdf = weights.cumsum(0).to(at::kFloat);

    TORCH_CHECK(
      cdf[-1].item().toFloat() > 0.0,
      "The sum of all the weights must be strictly greater than zero."
    );

    cdf /= cdf[-1];

    dim3 threads(threadsPerBlock);
    dim3 blocks((k + threadsPerBlock - 1)/threadsPerBlock);

    sampling_with_replacement_kernel<<<blocks, threads>>>(
      samples.data<int64_t>(),
      cdf.data<float>(),
      n,
      k,
      next_philox_seed
    );

    AT_CUDA_CHECK(cudaGetLastError());
  }

  return x.index_select(0, samples);
}

Tensor choice_cuda(
  const Tensor& input,
  int64_t k,
  bool replace,
  const Tensor& weights
){
  if (replace){
    return sampling_with_replacement_cuda(input, weights, k);
  } else {
    return reservoir_sampling_cuda(input, weights, k);
  }
}

Tensor choice_cuda(
  const Tensor& input,
  int64_t k,
  bool replace
){
  at::Tensor weights = at::empty({0}, input.options().dtype(at::kFloat));
  return native::choice_cuda(input, k, replace, weights);
}

}} // namespace at::native
