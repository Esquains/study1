#include "lazy_tensor_core/csrc/ops/ts_native_batch_norm_forward.h"

#include "lazy_tensor_core/csrc/ts_backend/ts_shape_inference.h"

namespace torch_lazy_tensors {
namespace ir {
namespace ops {

TSNativeBatchNormForward::TSNativeBatchNormForward(
    const torch::lazy::Value& input, const torch::lazy::Value& weight, const torch::lazy::Value& bias,
    const torch::lazy::Value& running_mean, const torch::lazy::Value& running_var, bool training,
    double momentum, double eps)
    : TsNode(torch::lazy::OpKind(at::aten::native_batch_norm),
           {input, weight, bias, running_mean, running_var},
           {ir::GetShapeFromTsValue(input), ir::GetShapeFromTsValue(running_mean),
            ir::GetShapeFromTsValue(running_var)},
           /*num_outputs=*/3,
           torch::lazy::MHash(training, momentum, eps)),
      training_(training),
      momentum_(momentum),
      eps_(eps) {}

std::string TSNativeBatchNormForward::ToString() const {
  std::stringstream ss;
  ss << TsNode::ToString() << ", training=" << training_
     << ", momentum=" << momentum_ << ", eps=" << eps_;
  return ss.str();
}

}  // namespace ops
}  // namespace ir
}  // namespace torch_lazy_tensors
