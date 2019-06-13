#include <ATen/ATen.h>
#include <ATen/AccumulateType.h>
#include <ATen/native/Pool.h>
#include <ATen/cuda/CUDAContext.h>
#include <ATen/cuda/CUDAApplyUtils.cuh>
#include <ATen/cuda/detail/TensorInfo.cuh>
#include <ATen/cuda/detail/IndexUtils.cuh>
#include <ATen/cuda/detail/KernelUtils.h>
#include <THC/THCNumerics.cuh>
#include <c10/macros/Macros.h>


namespace at {
namespace native {
namespace {

__device__ inline int min(int a, int b) {
  return a <= b ? a : b;
}

__device__ inline int max(int a, int b) {
  return a >= b ? a : b;
}

template <typename scalar_t, typename accscalar_t, bool COUNT_INCLUDE_PAD>
__global__ void avg_pool2d_out_cuda_frame(const int nthreads,
    const scalar_t* const bottom_data, const int num, const int channels,
    const int height, const int width, const int pooled_height,
    const int pooled_width, const int kernel_h, const int kernel_w,
    const int stride_h, const int stride_w, const int pad_h, const int pad_w,
    scalar_t* const top_data) {
  CUDA_KERNEL_LOOP(index, nthreads) {
    const int pw = index % pooled_width;
    const int ph = (index / pooled_width) % pooled_height;
    const int c = (index / pooled_width / pooled_height) % channels;
    const int n = index / pooled_width / pooled_height / channels;
    int hstart = ph * stride_h - pad_h;
    int wstart = pw * stride_w - pad_w;
    int hend = min(hstart + kernel_h, height + pad_h);
    int wend = min(wstart + kernel_w, width + pad_w);
    const int pool_size = (hend - hstart) * (wend - wstart);
    hstart = max(hstart, 0);
    wstart = max(wstart, 0);
    hend = min(hend, height);
    wend = min(wend, width);
    accscalar_t aveval = accscalar_t(0);
    const scalar_t* const bottom_slice = bottom_data + (n * channels + c) * height * width;
    for (int h = hstart; h < hend; ++h) {
      for (int w = wstart; w < wend; ++w) {
        aveval += bottom_slice[h * width + w];
      }
    }
    if(COUNT_INCLUDE_PAD)
      top_data[index] = ScalarConvert<accscalar_t, scalar_t>::to(aveval / pool_size);
    else
      top_data[index] = ScalarConvert<accscalar_t, scalar_t>::to(aveval / ((hend - hstart) * (wend - wstart)));
  }
}

template <typename scalar_t, typename accscalar_t, bool COUNT_INCLUDE_PAD>
__global__ void avg_pool2d_backward_out_cuda_frame(const int nthreads, const scalar_t* const top_diff,
    const int num, const int channels, const int height,
    const int width, const int pooled_height, const int pooled_width,
    const int kernel_h, const int kernel_w, const int stride_h,
    const int stride_w, const int pad_h, const int pad_w,
    scalar_t* const bottom_diff) {
  CUDA_KERNEL_LOOP(index, nthreads) {
    // find out the local index
    // find out the local offset
    const int w = index % width + pad_w;
    const int h = (index / width) % height + pad_h;
    const int c = (index / width / height) % channels;
    const int n = index / width / height / channels;
    const int phstart = (h < kernel_h) ? 0 : (h - kernel_h) / stride_h + 1;
    const int phend = min(h / stride_h + 1, pooled_height);
    const int pwstart = (w < kernel_w) ? 0 : (w - kernel_w) / stride_w + 1;
    const int pwend = min(w / stride_w + 1, pooled_width);
    accscalar_t gradient = accscalar_t(0);
    const scalar_t* const top_diff_slice =
        top_diff + (n * channels + c) * pooled_height * pooled_width;
    for (int ph = phstart; ph < phend; ++ph) {
      for (int pw = pwstart; pw < pwend; ++pw) {
        // figure out the pooling size
        int hstart = ph * stride_h - pad_h;
        int wstart = pw * stride_w - pad_w;
        int hend = min(hstart + kernel_h, height + pad_h);
        int wend = min(wstart + kernel_w, width + pad_w);
        int pool_size = (hend - hstart) * (wend - wstart);
        hstart = max(hstart, 0);
        wstart = max(wstart, 0);
        hend = min(hend, height);
        wend = min(wend, width);
        if(COUNT_INCLUDE_PAD)
          gradient += top_diff_slice[ph * pooled_width + pw] / pool_size;
        else
          gradient += top_diff_slice[ph * pooled_width + pw] / ((hend - hstart) * (wend - wstart));
      }
    }
    bottom_diff[index] = ScalarConvert<accscalar_t, scalar_t>::to(gradient);
  }
}

void avg_pool2d_out_cuda_template(
  Tensor& output,
  const Tensor& input_,
  IntArrayRef kernel_size,
  IntArrayRef stride,
  IntArrayRef padding,
  bool ceil_mode,
  bool count_include_pad)
{
  TensorArg output_arg{ output, "output", 1 };
  TensorArg input_arg{ input_, "input_", 2 };

  checkAllSameGPU("avg_pool2d_out_cuda", {output_arg, input_arg});

  // #20866 [JIT] stride.empty() is passed through
  // #20866 [LIBTORCH] IntegrationTest.MNIST: padding.size() == 1
  TORCH_INTERNAL_ASSERT(kernel_size.size() == 2 &&
                        (stride.empty() || stride.size() == 2) &&
                        (padding.size() == 1 || padding.size() == 2),
    "avg_pool2d: all IntArrayRef sizes must be 2");

  TORCH_CHECK((input_.ndimension() == 3 || input_.ndimension() == 4),
    "non-empty 3D or 4D (batch mode) tensor expected for input");

  const int kH = safe_downcast<int, int64_t>(kernel_size[0]);
  const int kW = safe_downcast<int, int64_t>(kernel_size[1]);

  const int dH = stride.empty() ? kH : safe_downcast<int, int64_t>(stride[0]);
  const int dW = stride.empty() ? kW : safe_downcast<int, int64_t>(stride[1]);

  const int padH = safe_downcast<int, int64_t>(padding[0]);
  const int padW = padding.size() == 1 ? padH : safe_downcast<int, int64_t>(padding[1]);

  const int64_t nbatch = input_.ndimension() == 4 ? input_.size(-4) : 1;
  const int64_t nInputPlane = input_.size(-3);
  const int64_t inputHeight = input_.size(-2);
  const int64_t inputWidth = input_.size(-1);

  const int64_t outputWidth = pooling_output_shape<int64_t>(inputWidth, kW, padW, dW, 1, ceil_mode);
  const int64_t outputHeight = pooling_output_shape<int64_t>(inputHeight, kH, padH, dH, 1, ceil_mode);

  pool2d_shape_check(
    input_,
    kH, kW, dH, dW, padH, padW, 1, 1,
    nInputPlane,
    inputHeight, inputWidth,
    outputHeight, outputWidth);

  Tensor input = input_.contiguous();

  output.resize_({nbatch, nInputPlane, outputHeight, outputWidth});

  const int count = safe_downcast<int, int64_t>(output.numel());
  const int num_threads = std::min(at::cuda::getCurrentDeviceProperties()->maxThreadsPerBlock, 1024);

  if (count_include_pad) {
    AT_DISPATCH_FLOATING_TYPES_AND_HALF(input.scalar_type(),
      "avg_pool2d_out_cuda_frame",
      [&] {
        using accscalar_t = acc_type<scalar_t, true>;

        scalar_t *output_data = output.data<scalar_t>();
        scalar_t *input_data = input.data<scalar_t>();

        avg_pool2d_out_cuda_frame<scalar_t, accscalar_t, true>
          <<<cuda::ATenCeilDiv(count, num_threads), num_threads, 0, at::cuda::getCurrentCUDAStream()>>>(
            count,
            input_data,
            nbatch,
            nInputPlane,
            inputHeight, inputWidth,
            outputHeight, outputWidth,
            kH, kW,
            dH, dW,
            padH, padW,
            output_data);
      }
    );
  }
  else {
    AT_DISPATCH_FLOATING_TYPES_AND_HALF(input.scalar_type(),
      "avg_pool2d_out_cuda_frame",
      [&] {
        using accscalar_t = acc_type<scalar_t, true>;

        scalar_t *output_data = output.data<scalar_t>();
        scalar_t *input_data = input.data<scalar_t>();

        avg_pool2d_out_cuda_frame<scalar_t, accscalar_t, false>
          <<<cuda::ATenCeilDiv(count, num_threads), num_threads, 0, at::cuda::getCurrentCUDAStream()>>>(
             count,
             input_data,
             nbatch,
             nInputPlane,
             inputHeight, inputWidth,
             outputHeight, outputWidth,
             kH, kW,
             dH, dW,
             padH, padW,
             output_data);
      }
    );
  }
  
  TORCH_CHECK(cudaGetLastError() == cudaSuccess,
     "avg_pool2d_out_cuda_frame failed with error code ",
     cudaGetLastError());

  if (input.ndimension() == 3) {
    output.resize_({nInputPlane, outputHeight, outputWidth});
  }
}

Tensor& avg_pool2d_backward_out_cuda_template(
  Tensor& gradInput,
  const Tensor& gradOutput_,
  const Tensor& input_,
  IntArrayRef kernel_size,
  IntArrayRef stride,
  IntArrayRef padding,
  bool ceil_mode,
  bool count_include_pad)
{
  TensorArg gradInput_arg{ gradInput, "gradInput", 1 };
  TensorArg gradOutput_arg{ gradOutput_, "gradOutput_", 2 };
  TensorArg input_arg{ input_, "input_", 3 };

  checkAllSameGPU("avg_pool2d_backward_out_cuda",
                  {gradInput_arg, gradOutput_arg, input_arg});

  // #20866 [JIT] stride.empty() is passed through
  // #20866 [LIBTORCH] IntegrationTest.MNIST: padding.size() == 1
  TORCH_INTERNAL_ASSERT(kernel_size.size() == 2 &&
                        (stride.empty() || stride.size() == 2) &&
                        (padding.size() == 1 || padding.size() == 2),
    "avg_pool2d: all IntArrayRef sizes must be 2");

  TORCH_CHECK((input_.ndimension() == 3 || input_.ndimension() == 4),
    "non-empty 3D or 4D (batch mode) tensor expected for input");

  const int kH = safe_downcast<int, int64_t>(kernel_size[0]);
  const int kW = safe_downcast<int, int64_t>(kernel_size[1]);

  const int dH = stride.empty() ? kH : safe_downcast<int, int64_t>(stride[0]);
  const int dW = stride.empty() ? kW : safe_downcast<int, int64_t>(stride[1]);

  const int padH = safe_downcast<int, int64_t>(padding[0]);
  const int padW = padding.size() == 1 ? padH : safe_downcast<int, int64_t>(padding[1]);

  const Tensor input = input_.contiguous();
  const Tensor gradOutput = gradOutput_.contiguous();

  const int64_t nbatch = input.ndimension() == 4 ? input.size(-4) : 1;
  const int64_t nInputPlane = input.size(-3);
  const int64_t inputHeight = input.size(-2);
  const int64_t inputWidth = input.size(-1);

  const int64_t outputHeight = pooling_output_shape<int64_t>(inputHeight, kH, padH, dH, 1, ceil_mode);
  const int64_t outputWidth = pooling_output_shape<int64_t>(inputWidth, kW, padW, dW, 1, ceil_mode);

  avg_pool2d_backward_shape_check(
    input_,
    gradOutput_,
    nbatch,
    kH, kW, dH, dW, padH, padW,
    nInputPlane,
    inputHeight, inputWidth,
    outputHeight, outputWidth);

  gradInput.resize_as_(input);

  const int count =  safe_downcast<int, int64_t>(input.numel());
  const int num_threads = std::min(at::cuda::getCurrentDeviceProperties()->maxThreadsPerBlock, 1024);

  if (count_include_pad) {
    AT_DISPATCH_FLOATING_TYPES_AND_HALF(input.scalar_type(),
      "avg_pool2d_backward_out_cuda_frame",
      [&] {
        using accscalar_t = acc_type<scalar_t, true>;

        scalar_t *gradOutput_data = gradOutput.data<scalar_t>();
        scalar_t *gradInput_data = gradInput.data<scalar_t>();

        avg_pool2d_backward_out_cuda_frame<scalar_t, accscalar_t, true>
          <<<cuda::ATenCeilDiv(count, num_threads), num_threads, 0, at::cuda::getCurrentCUDAStream()>>>(
             count,
             gradOutput_data,
             nbatch,
             nInputPlane,
             inputHeight, inputWidth,
             outputHeight, outputWidth,
             kH, kW,
             dH, dW,
             padH, padW,
             gradInput_data);
      }
    );
  }
  else {
    AT_DISPATCH_FLOATING_TYPES_AND_HALF(input.scalar_type(),
      "avg_pool2d_backward_out_cuda_frame",
      [&] {
        using accscalar_t = acc_type<scalar_t, true>;

        scalar_t *gradOutput_data = gradOutput.data<scalar_t>();
        scalar_t *gradInput_data = gradInput.data<scalar_t>();

        avg_pool2d_backward_out_cuda_frame<scalar_t, accscalar_t, false>
          <<<cuda::ATenCeilDiv(count, num_threads), num_threads, 0, at::cuda::getCurrentCUDAStream()>>>(
             count,
             gradOutput_data,
             nbatch,
             nInputPlane,
             inputHeight, inputWidth,
             outputHeight, outputWidth,
             kH, kW,
             dH, dW,
             padH, padW,
             gradInput_data);
      }
    );
  }

  TORCH_CHECK(cudaGetLastError() == cudaSuccess,
    "avg_pool2d_backward_out_cuda failed with error code ",
    cudaGetLastError());

  return gradInput;
}

} // namespace

Tensor& avg_pool2d_out_cuda(
  Tensor& output,
  const Tensor& input,
  IntArrayRef kernel_size,
  IntArrayRef stride,
  IntArrayRef padding,
  bool ceil_mode,
  bool count_include_pad)
{
  avg_pool2d_out_cuda_template(
   output,
   input,
   kernel_size,
   stride,
   padding,
   ceil_mode,
   count_include_pad);
  return output;
}

Tensor avg_pool2d_cuda(
  const Tensor& input,
  IntArrayRef kernel_size,
  IntArrayRef stride,
  IntArrayRef padding,
  bool ceil_mode,
  bool count_include_pad)
{
  Tensor output = at::empty({0}, input.options());
  avg_pool2d_out_cuda_template(
    output,
    input,
    kernel_size,
    stride,
    padding,
    ceil_mode,
    count_include_pad);
  return output;
}

Tensor& avg_pool2d_backward_out_cuda(
  Tensor& gradInput,
  const Tensor& gradOutput_,
  const Tensor& input,
  IntArrayRef kernel_size,
  IntArrayRef stride,
  IntArrayRef padding,
  bool ceil_mode,
  bool count_include_pad)
{
  avg_pool2d_backward_out_cuda_template(
    gradInput,
    gradOutput_,
    input,
    kernel_size,
    stride,
    padding,
    ceil_mode,
    count_include_pad);
  return gradInput;
}

Tensor avg_pool2d_backward_cuda(
  const Tensor& gradOutput_,
  const Tensor& input,
  IntArrayRef kernel_size,
  IntArrayRef stride,
  IntArrayRef padding,
  bool ceil_mode,
  bool count_include_pad)
{
  auto gradInput = at::zeros_like(input);
  avg_pool2d_backward_out_cuda_template(
    gradInput,
    gradOutput_,
    input,
    kernel_size,
    stride,
    padding,
    ceil_mode,
    count_include_pad);
  return gradInput;
}

} // at::native
} // at
