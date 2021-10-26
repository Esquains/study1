#pragma once

#include "lazy_tensor_core/csrc/ts_backend/TsNode.h"

namespace torch_lazy_tensors {
namespace ir {
namespace ops {

class Resize : public TsNode {
 public:
  Resize(const torch::lazy::Value& input, std::vector<int64_t> size);

  NodePtr Clone(OpList operands) const override;

  std::string ToString() const override;

  const std::vector<int64_t>& size() const { return size_; }

 private:
  std::vector<int64_t> size_;
};

}  // namespace ops
}  // namespace ir
}  // namespace torch_lazy_tensors
