#include <ATen/ATen.h>
#include <ATen/NativeFunctions.h>
#include <ATen/core/op_registration/op_registration.h>
#include <ATen/native/TensorIterator.h>
#include <ATen/native/cpu/Loops.h>
#include <ATen/native/quantized/cpu/quantized_ops.h>
#include <ATen/quantized/Quantizer.h>
#include <ATen/native/quantized/cpu/init_qnnpack.h>
#include <ATen/native/quantized/cpu/qnnpack_utils.h>
#include <c10/core/TensorOptions.h>
#include <caffe2/utils/threadpool/ThreadPoolMobile.h>

#include <algorithm>

namespace at {
namespace native {

namespace {
Tensor quantized_channel_shuffle_impl(
    const Tensor& self,
    int64_t groups) {

  TORCH_CHECK(
      self.dim() == 4,
      "channel_shuffle expects 4D input, but got input with sizes ",
      self.sizes());
  TORCH_CHECK(
      self.scalar_type() == kQUInt8,
      "Quantized channel shuffle works only on uint8_t.",
      "But got:", self.scalar_type());
  const Tensor self_nhwc = self.contiguous(MemoryFormat::ChannelsLast);
  Tensor qy = at::new_qtensor_cpu(
      self_nhwc.sizes(),
      TensorOptions(kQUInt8)
          .device(kCPU)
          .memory_format(self_nhwc.suggest_memory_format()),
      make_per_tensor_affine_quantizer(
          self_nhwc.q_scale(), self_nhwc.q_zero_point(), kQUInt8)
      );

  // Degenerate case of just copying.
  if (groups == 1) {
    qy.copy_(self_nhwc);
    return qy.contiguous(self.suggest_memory_format());
  }

  int64_t channels = self.size(1);
  TORCH_CHECK((channels % groups) == 0,
             "Number of channels must be divisible gy groups. Got ",
             channels, " channels and ", groups, " groups.");

  initQNNPACK();

  pytorch_qnnp_operator_t qnnpack_operator{nullptr};

  const pytorch_qnnp_status createStatus = pytorch_qnnp_create_channel_shuffle_nc_x8(
      groups /* groups */,
      channels / groups /* group channels */,
      0 /* flags */,
      &qnnpack_operator);
  TORCH_INTERNAL_ASSERT(
      createStatus == pytorch_qnnp_status_success,
      "failed to create QNNPACK Add operator");

  const pytorch_qnnp_status setupStatus = pytorch_qnnp_setup_channel_shuffle_nc_x8(
      qnnpack_operator,
      self_nhwc.numel() / channels /* batch size */,
      (uint8_t*)self_nhwc.data_ptr<c10::quint8>() /* slef data */,
      channels /* self stride */,
      (uint8_t*)qy.data_ptr<c10::quint8>() /* qy data */,
      channels /* qy stride */);
  TORCH_INTERNAL_ASSERT(
      setupStatus == pytorch_qnnp_status_success,
      "failed to setup QNNPACK Add operator");

#ifdef FBCODE_CAFFE2
  const pytorch_qnnp_status runStatus =
      pytorch_qnnp_run_operator(qnnpack_operator, nullptr /* thread pool */);
#else
  pthreadpool_t threadpool = caffe2::mobile_pthreadpool();
  const pytorch_qnnp_status runStatus =
      pytorch_qnnp_run_operator(qnnpack_operator, threadpool);
#endif
  TORCH_INTERNAL_ASSERT(
      runStatus == pytorch_qnnp_status_success,
      "failed to run QNNPACK Add operator");

  return qy.contiguous(self.suggest_memory_format());
}
} // namespace

// at::native functions for the native_functions.yaml
Tensor quantized_channel_shuffle(
    const Tensor& self,
    int64_t groups) {
  return quantized_channel_shuffle_impl(self, groups);
}

// Keep the registry in the anonymous namespace.
namespace {
class QChannelShuffle final : public c10::OperatorKernel {
 public:
  Tensor operator()(Tensor qx, int64_t groups) {
    return quantized_channel_shuffle_impl(qx, groups);
  }
};

static auto registry = c10::RegisterOperators().op(
    "quantized::channel_shuffle(Tensor qx, int groups) -> Tensor qy",
    c10::RegisterOperators::options()
        .aliasAnalysis(at::AliasAnalysisKind::FROM_SCHEMA)
        .kernel<QChannelShuffle>(DispatchKey::QuantizedCPUTensorId));
} // namespace

} // namespace native
} // namespace at
