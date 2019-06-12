#include <ATen/ATen.h>
#include <ATen/AccumulateType.h>
#include <ATen/NativeFunctions.h>
#include <ATen/TensorUtils.h>
#include <ATen/Utils.h>
#include <ATen/cuda/CUDAContext.h>
#include <ATen/cuda/CUDAApplyUtils.cuh>
#include <ATen/native/cuda/UpSample.cuh>

namespace at {
namespace native {
namespace {

#define UNROLL_FACTOR 4
#define MAX_THREADS 512

static int lastPow2(unsigned int n) {
  n |= (n >> 1);
  n |= (n >> 2);
  n |= (n >> 4);
  n |= (n >> 8);
  n |= (n >> 16);
  return n - (n >> 1);
}

template <typename scalar_t, typename accscalar_t>
C10_LAUNCH_BOUNDS_1(1024)
__global__ void upsample_nearest2d_out_frame(
    const scalar_t* idata,
    scalar_t* odata,
    const size_t nc,
    const size_t height1,
    const size_t width1,
    const size_t height2,
    const size_t width2) {
  int nc_iter = threadIdx.z + blockIdx.z * blockDim.z;
  int w2 = threadIdx.x + blockIdx.x * blockDim.x;
  int h2 = threadIdx.y + blockIdx.y * blockDim.y;

  if (w2 >= width2 || h2 >= height2) {
    return;
  }

  const float height_scale = (float)height1 / (float)height2;
  const float width_scale = (float)width1 / (float)width2;
  int nc_stride = blockDim.z * gridDim.z;

  const size_t h1 = height1 == height2
      ? h2
      : nearest_neighbor_compute_source_index(height_scale, h2, height1);
  const size_t w1 = width1 == width2
      ? w2
      : nearest_neighbor_compute_source_index(width_scale, w2, width1);

  size_t src_index = (nc_iter * height1 + h1) * width1 + w1;
  size_t src_index_stride = nc_stride * width1 * height1;
  size_t dst_index = (nc_iter * height2 + h2) * width2 + w2;
  size_t dst_index_stride = nc_stride * width2 * height2;

  // iterating over
  while (nc_iter < nc) {
    odata[dst_index] = idata[src_index];
    dst_index += dst_index_stride;
    src_index += src_index_stride;
    nc_iter += nc_stride;
  }
}

template <typename scalar_t, typename accscalar_t>
C10_LAUNCH_BOUNDS_1(1024)
__global__ void interpolate_nearest_dgrad_chw(
    const scalar_t* grad_o,
    size_t dim_b,
    size_t dim_c,
    size_t src_dim_h,
    size_t src_dim_w,
    size_t dst_dim_h,
    size_t dst_dim_w,
    scalar_t* grad_i) {
  assert(gridDim.y == 1);
  assert(gridDim.z == 1);
  assert(blockDim.y == 1);
  assert(blockDim.z == 1);

  size_t dst_idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (dst_idx >= dim_c * dst_dim_h * dst_dim_w)
    return;

  int dst_c_stride = dst_dim_h * dst_dim_w;
  int src_c_stride = src_dim_h * src_dim_w;

  int c = (dst_idx / (dst_c_stride)) % dim_c;

  float scale_factor = (float)src_dim_h / dst_dim_h;
  int dst_y = (dst_idx / dst_dim_w) % dst_dim_h;
  int src_y = (int)ceilf(dst_y * scale_factor);
  int src_y_up = min((int)ceilf((dst_y + 1) * scale_factor), (int)src_dim_h);

  scale_factor = (float)src_dim_w / dst_dim_w;
  int dst_x = dst_idx % dst_dim_w;
  int src_x = (int)ceilf(dst_x * scale_factor);
  int src_x_up = min((int)ceilf((dst_x + 1) * scale_factor), (int)src_dim_w);

  for (int b = 0; b < dim_b; b++) {
    accscalar_t grad = 0;
    for (int y = src_y; y < src_y_up; y++) {
      for (int x = src_x; x < src_x_up; x++) {
        size_t src_idx =
            b * dim_c * src_c_stride + c * src_c_stride + y * src_dim_w + x;
        grad += grad_o[src_idx];
      }
    }
    grad_i[dst_idx] = grad;
    dst_idx += dim_c * dst_c_stride;
  }
}

// Backward operation
template <typename scalar_t, typename accscalar_t, unsigned int N>
C10_LAUNCH_BOUNDS_1(1024)
__global__ void upsample_nearest2d_backward_out_frame(
    const int width_per_block,
    scalar_t* idata,
    const scalar_t* odata,
    const size_t nc,
    const size_t height1,
    const size_t width1,
    const size_t height2,
    const size_t width2) {
  // shared memory used for reduction;
  extern __shared__ char smem[];

  const float height_scale = (float)height1 / (float)height2;
  const float width_scale = (float)width1 / (float)width2;

  int nc_index = threadIdx.z + blockIdx.z * blockDim.z;
  int nc_stride = gridDim.z * blockDim.z;
  // offset computed.
  int block_offset_w1 = blockIdx.x * width_per_block;
  int block_offset_w2 = nearest_neighbor_compute_destination_index(
      width_scale, block_offset_w1, width2);
  int w2 = threadIdx.x + block_offset_w2;
  int h1 = threadIdx.y + blockIdx.y * blockDim.y;

  accscalar_t acc[N]; // initialized to 0.0 inside the loop;
  accscalar_t* buffer = (accscalar_t*)smem;
  int index = threadIdx.x * (blockDim.y * blockDim.z + 1) +
      threadIdx.y * blockDim.z + threadIdx.z;
  int buffer_stride = blockDim.y * blockDim.z + 1;

  // accumulation across column
  const int h2_offset =
      nearest_neighbor_compute_destination_index(height_scale, h1, height2);
  const int h2_boundary = nearest_neighbor_compute_destination_index(
      height_scale, h1 + 1, height2 + 1);

#pragma unroll
  for (int i = 0; i < N; i++) {
    acc[i] = 0.0;
    int nc_iter = nc_index + i * nc_stride;
    if (w2 < width2 && nc_iter < nc) {
      int h2 = h2_offset;
      size_t index = (nc_iter * height2 + h2) * width2 + w2;
      while (h2 < h2_boundary) {
        acc[i] += odata[index];
        index += width2;
        h2++;
      }
    }
    // write to shared_mem
    buffer[index + i * buffer_stride * blockDim.x] = acc[i];
  }

  __syncthreads();

  int t_id = threadIdx.z * blockDim.y * blockDim.x + threadIdx.y * blockDim.x +
      threadIdx.x;

  // accumulation across row and write to output
  if (t_id < blockDim.z * blockDim.y * width_per_block) {
    // adjust block layout to accommodate shrinked block x dimension.
    const int layout_x = t_id % width_per_block;
    int tmp_id = t_id / width_per_block;
    const int layout_y = tmp_id % blockDim.y;
    const int layout_z = tmp_id / blockDim.y;

    // accumulate across row;
    int w1 = layout_x + blockIdx.x * width_per_block;
    nc_index = layout_z + blockIdx.z * blockDim.z;
    h1 = layout_y + blockIdx.y * blockDim.y;
    int w2_offset =
        nearest_neighbor_compute_destination_index(width_scale, w1, width2);
    int w_buffer_offset = w2_offset - block_offset_w2;
    const int acc_width_length = nearest_neighbor_compute_destination_index(
                                     width_scale, w1 + 1, width2 + 1) -
        w2_offset;
    int offset = w_buffer_offset * (blockDim.y * blockDim.z + 1) +
        layout_y * blockDim.z + layout_z;
#pragma unroll
    for (int i = 0; i < N; i++) {
      acc[i] = 0.0;
      int buffer_index = offset + i * buffer_stride * blockDim.x;
      int nc_iter = nc_index + i * nc_stride;
      for (int j = 0; j < acc_width_length && h1 < height1 && nc_iter < nc;
           j++) {
        acc[i] += buffer[buffer_index];
        buffer_index += buffer_stride;
      }

      // write output
      if (nc_iter < nc && h1 < height1 && w1 < width1) {
        size_t index = (nc_iter * height1 + h1) * width1 + w1;
        idata[index] = acc[i];
      }
    }
  }
}

static void upsample_nearest2d_out_cuda_template(
    Tensor& output,
    const Tensor& input_,
    IntArrayRef output_size) {
  TensorArg input_arg{input_, "input_", 1}, output_arg{output, "output", 2};
  checkAllSameGPU(
      "upsample_nearest2d_out_cuda_template", {input_arg, output_arg});

  TORCH_CHECK(
      output_size.size() == 2,
      "It is expected output_size equals to 2, but got size ",
      output_size.size());

  int output_height = output_size[0];
  int output_width = output_size[1];

  int nbatch = input_.size(0);
  int channels = input_.size(1);
  int input_height = input_.size(2);
  int input_width = input_.size(3);

  upsample_2d_shape_check(
      input_,
      Tensor(),
      nbatch,
      channels,
      input_height,
      input_width,
      output_height,
      output_width);

  AT_ASSERT(
      input_height > 0 && input_width > 0 && output_height > 0 &&
      output_width > 0);

  Tensor input = input_.contiguous();
  output.resize_({nbatch, channels, output_height, output_width});
  output.zero_();

  int nc = nbatch * channels;

  const int max_threads = std::min<int>(
      at::cuda::getCurrentDeviceProperties()->maxThreadsPerBlock, MAX_THREADS);

  int block_x = std::min<int>(lastPow2(output_width), max_threads);
  int block_y = std::min<int>(lastPow2(output_height), max_threads / block_x);
  int block_z = std::min<int>(nc, max_threads / block_x / block_y);
  const dim3 block(block_x, block_y, block_z);

  int grid_x = cuda::ATenCeilDiv(output_width, block_x);
  int grid_y = cuda::ATenCeilDiv(output_height, block_y);
  // not neccessary to use UNROLL_FACTOR here, increase workload per thread for
  // better performance. This is simpler than the backward kernel, as there's
  // no reduction in forward kernel.
  int grid_z = cuda::ATenCeilDiv(nc, block_z * UNROLL_FACTOR);
  const dim3 grid(grid_x, grid_y, grid_z);

  cudaStream_t stream = at::cuda::getCurrentCUDAStream();
  AT_DISPATCH_FLOATING_TYPES_AND_HALF(
      input.scalar_type(), "upsample_nearest2d_out_frame", [&] {
        using accscalar_t = at::acc_type<scalar_t, true>;

        auto idata = input.data<scalar_t>();
        auto odata = output.data<scalar_t>();

        upsample_nearest2d_out_frame<scalar_t, accscalar_t>
            <<<grid, block, 0, stream>>>(
                idata,
                odata,
                nc,
                input_height,
                input_width,
                output_height,
                output_width);
      });

  AT_CUDA_CHECK(cudaGetLastError());
}

static void upsample_nearest2d_backward_out_cuda_template(
    Tensor& grad_input,
    const Tensor& grad_output_,
    IntArrayRef output_size,
    IntArrayRef input_size) {
  TensorArg grad_input_arg{grad_input, "grad_input", 1},
      grad_output_arg{grad_output_, "grad_output_", 2};
  checkAllSameGPU(
      "upsample_nearest2d_backward_out_cuda",
      {grad_output_arg, grad_input_arg});

  TORCH_CHECK(
      output_size.size() == 2,
      "It is expected output_size equals to 2, but got size ",
      output_size.size());

  TORCH_CHECK(
      input_size.size() == 4,
      "It is expected input_size equals to 4, but got size ",
      input_size.size());

  int output_height = output_size[0];
  int output_width = output_size[1];

  int nbatch = input_size[0];
  int channels = input_size[1];
  int input_height = input_size[2];
  int input_width = input_size[3];

  upsample_2d_shape_check(
      Tensor(),
      grad_output_,
      nbatch,
      channels,
      input_height,
      input_width,
      output_height,
      output_width);

  Tensor grad_output = grad_output_.contiguous();
  grad_input.resize_({nbatch, channels, input_height, input_width});

  grad_input.zero_();

  int nc = nbatch * channels;

  // UpSampling/DownSampling requires different kernels in order to get
  // efficient launch configs
  if (input_width >= output_width) {
    unsigned int n = grad_input.numel() / nbatch;
    dim3 bdim{std::min<int>(
        at::cuda::getCurrentDeviceProperties()->maxThreadsPerBlock,
        MAX_THREADS)};
    dim3 gdim{cuda::ATenCeilDiv(n, bdim.x)};
    cudaStream_t stream = at::cuda::getCurrentCUDAStream();
    AT_DISPATCH_FLOATING_TYPES_AND_HALF(
        grad_output.scalar_type(), "interpolate_nearest_dgrad_chw", [&] {
          using accscalar_t = at::acc_type<scalar_t, true>;

          auto idata = grad_input.data<scalar_t>();
          auto odata = grad_output.data<scalar_t>();

          interpolate_nearest_dgrad_chw<scalar_t, accscalar_t>
              <<<gdim, bdim, 0, stream>>>(
                  odata,
                  nbatch,
                  channels,
                  output_height,
                  output_width,
                  input_height,
                  input_width,
                  idata);
        });
  } else {
    const int max_threads = std::min<int>(
        at::cuda::getCurrentDeviceProperties()->maxThreadsPerBlock,
        MAX_THREADS);

    int block_x = std::min<int>(lastPow2(output_width), max_threads / 4);
    // this is intended to be a floorf(block.x / (float)width_scale);
    int width_per_block = block_x * input_width / output_width;
    // we let each thread loop over reduced-column;
    int block_y = std::min<int>(lastPow2(input_height), max_threads / block_x);
    int block_z = std::min<int>(
        cuda::ATenCeilDiv(nc, UNROLL_FACTOR), max_threads / block_x / block_y);
    const dim3 block(block_x, block_y, block_z);

    int grid_x = cuda::ATenCeilDiv(output_width, block_x);
    int grid_y = cuda::ATenCeilDiv(input_height, block_y);
    int grid_z = cuda::ATenCeilDiv(nc, block_z * UNROLL_FACTOR);
    const dim3 grid(grid_x, grid_y, grid_z);

    cudaStream_t stream = at::cuda::getCurrentCUDAStream();
    AT_DISPATCH_FLOATING_TYPES_AND_HALF(
        grad_output.scalar_type(),
        "upsample_nearest2d_backward_out_frame",
        [&] {
          using accscalar_t = at::acc_type<scalar_t, true>;

          auto idata = grad_input.data<scalar_t>();
          auto odata = grad_output.data<scalar_t>();

          // shared memory used for row reduction;
          // padded to avoid bank conflict;
          size_t mem_size =
              block_x * (1 + block_y * block_z) * sizeof(accscalar_t);

          upsample_nearest2d_backward_out_frame<
              scalar_t,
              accscalar_t,
              UNROLL_FACTOR><<<grid, block, mem_size * UNROLL_FACTOR, stream>>>(
              width_per_block,
              idata,
              odata,
              nc,
              input_height,
              input_width,
              output_height,
              output_width);
        });
  }
  AT_CUDA_CHECK(cudaGetLastError());
}

} // namespace

Tensor& upsample_nearest2d_out_cuda(
    Tensor& output,
    const Tensor& input,
    IntArrayRef output_size) {
  upsample_nearest2d_out_cuda_template(output, input, output_size);
  return output;
}

Tensor upsample_nearest2d_cuda(const Tensor& input, IntArrayRef output_size) {
  Tensor output = at::empty_like(input);
  upsample_nearest2d_out_cuda_template(output, input, output_size);
  return output;
}

Tensor& upsample_nearest2d_backward_out_cuda(
    Tensor& grad_input,
    const Tensor& grad_output,
    IntArrayRef output_size,
    IntArrayRef input_size) {
  upsample_nearest2d_backward_out_cuda_template(
      grad_input, grad_output, output_size, input_size);
  return grad_input;
}

Tensor upsample_nearest2d_backward_cuda(
    const Tensor& grad_output,
    IntArrayRef output_size,
    IntArrayRef input_size) {
  Tensor grad_input = at::empty_like(grad_output);
  upsample_nearest2d_backward_out_cuda_template(
      grad_input, grad_output, output_size, input_size);
  return grad_input;
}

} // namespace native
} // namespace at
