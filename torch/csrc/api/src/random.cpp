#include <torch/random.h>

#include <ATen/Context.h>

#include <TH/TH.h>
#include <ATen/Context.h>
#include <torch/types.h>
#include <torch/csrc/utils/tensor_types.h>

namespace {

torch::Tensor _get_rng_state(torch::Device device) {
  auto& default_generator = torch::globalContext().defaultGenerator(device);

  auto tensor = torch::empty({0}, torch::device(torch::kCPU).dtype(torch::kByte));
  if (default_generator.device().type() == torch::kCPU) {
    THByteTensor_getRNGState(&default_generator, (THByteTensor*)(tensor.unsafeGetTensorImpl()));
  } else {
#ifdef USE_CUDA
    TORCH_INTERNAL_ASSERT(default_generator.device().type() == torch::kCUDA);
    THCRandom_getRNGState(&default_generator, (THByteTensor*)(tensor.unsafeGetTensorImpl()));
#else
    TORCH_INTERNAL_ASSERT(false, "libtorch not compiled with CUDA");
#endif
  }
  return std::move(tensor);
}

void _set_rng_state(torch::Tensor new_state, torch::Device device) {
  auto tensor = new_state.clone();
  auto& default_generator = torch::globalContext().defaultGenerator(device);

  TORCH_CHECK(
    tensor.layout() == torch::kStrided &&
    tensor.device().type() == torch::kCPU &&
    tensor.scalar_type() == torch::kByte,
    "expected a Tensor with options `torch::device(torch::kCPU).layout(torch::kStrided).dtype(torch::kByte)`, ",
    "but got Tensor with device type: ", tensor.device().type(),
    ", layout: ", tensor.layout(),
    ", scalar type: ", tensor.scalar_type());

  if (default_generator.device().type() == torch::kCPU) {
    THByteTensor_setRNGState(&default_generator, (THByteTensor*)tensor.unsafeGetTensorImpl());
  } else {
#ifdef USE_CUDA
    TORCH_INTERNAL_ASSERT(default_generator.device().type() == torch::kCUDA);
    THCRandom_setRNGState(&default_generator, (THByteTensor*)tensor.unsafeGetTensorImpl());
#else
    TORCH_INTERNAL_ASSERT(false, "libtorch not compiled with CUDA");
#endif
  }
}

}

namespace torch {

Tensor get_rng_state() {
  return _get_rng_state(torch::Device(torch::kCPU));
}

void set_rng_state(Tensor new_state) {
  _set_rng_state(new_state, torch::Device(torch::kCPU));
}

namespace cuda {

Tensor get_rng_state(torch::Device device) {
  // yf225 TODO: assert that device is not CPU. Does this actually work on Python?
  return _get_rng_state(device);
}

void set_rng_state(Tensor new_state, torch::Device device) {
  // yf225 TODO: assert that device is not CPU. Does this actually work on Python?
  _set_rng_state(new_state, device);
}

}

}  // namespace torch
