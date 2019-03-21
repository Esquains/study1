#include <ATen/ATen.h>
#include <ATen/Dispatch.h>
#include <ATen/NativeFunctions.h>

#include <ATen/native/Cross.h>

namespace at { namespace native {

DEFINE_DISPATCH(cross_stub);

Tensor cross(const Tensor & input, const Tensor & other, const c10::optional<int64_t> dimension) {
  Tensor out = at::empty_like(input);
  native::cross_out(out, input, other, dimension);
  return out;
}

Tensor & cross_out(Tensor & out, const Tensor & input, const Tensor & other, const c10::optional<int64_t> dimension) {
  if (out.sizes() != input.sizes()) {
    out.resize_as_(input);
  }
  auto device_res = input.type().device_type();
  AT_CHECK(device_res == kCPU || device_res == kCUDA, "cross only supports CPU and CUDA devices, out got: ", device_res);
  auto device1 = input.type().device_type();
  AT_CHECK(device1 == kCPU || device1 == kCUDA, "cross only supports CPU and CUDA devices, input got: ", device1);
  auto device2 = other.type().device_type();
  AT_CHECK(device2 == kCPU || device2 == kCUDA, "cross only supports CPU and CUDA devices, other got: ", device2);
  AT_CHECK(device_res == device1, "out and input must have the same device type. out: ", device_res, " input: ", device1);
  AT_CHECK(device1 == device2, "input and other must have the same device type. input: ", device1, " other: ", device2);
  AT_CHECK(!out.is_cuda() || out.get_device() == input.get_device(), "device of out (", input.get_device(), ") must match device of input (", other.get_device(), ")");
  AT_CHECK(!input.is_cuda() || input.get_device() == other.get_device(), "device of input (", input.get_device(), ") must match device of other (", other.get_device(), ")");
  AT_CHECK(input.dim() == other.dim(), "inconsistent tensors dimensions input: ", input.dim(), " other: ", other.dim());
  for(int64_t i = 0; i < input.dim(); i++) {
    AT_CHECK(input.size(i) == other.size(i), "inconsistent tensors sizes at dim=", i, " input: ", input.size(i), " other: ", other.size(i));
  }

  int64_t dim = -1;
  if(!dimension.has_value()) {
    for(int64_t i = 0; i < input.dim(); i++) {
      if(input.size(i) == 3) {
        dim = i;
        break;
      }
    }
    AT_CHECK(dim >= 0, "no dimension of size 3 in input");
  } else {
    dim = maybe_wrap_dim(dimension.value(), input.dim());
  }
  AT_CHECK(input.size(dim) == 3, "dimension ", dim, " does not have size 3");

  cross_stub(device1, out, input, other, dim);
  return out;
}

}} // namespace at::native

