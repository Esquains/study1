#ifndef CAFFE2_UTILS_TEST_UTILS_H_
#define CAFFE2_UTILS_TEST_UTILS_H_

#include "caffe2/core/tensor.h"
#include "caffe2/core/workspace.h"

#include <gtest/gtest.h>

// Utilities that make it easier to write caffe2 C++ unit tests.
namespace caffe2 {
namespace testing {

// Asserts that the numeric values of two tensors are the same.
template <typename T>
void assertsTensorEquals(const TensorCPU& tensor1, const TensorCPU& tensor2) {
  EXPECT_EQ(tensor1.sizes(), tensor2.sizes());
  for (auto idx = 0; idx < tensor1.numel(); ++idx) {
    EXPECT_EQ(tensor1.data<T>()[idx], tensor2.data<T>()[idx]);
  }
}

// Read a tensor from the workspace.
const caffe2::Tensor& getTensor(
    const caffe2::Workspace& workspace,
    const std::string& name);

// Create a new tensor in the workspace.
caffe2::Tensor* createTensor(
    const std::string& name,
    caffe2::Workspace* workspace);

// Create a new operator in the net.
caffe2::OperatorDef* createOperator(
    const std::string& type,
    const std::vector<string>& inputs,
    const std::vector<string>& outputs,
    caffe2::NetDef* net);

// Coincise util class to mutate a net in a chaining fashion.
struct NetMutator {
  explicit NetMutator(caffe2::NetDef* net) : net_(net) {}

  NetMutator& newOp(
      const std::string& type,
      const std::vector<string>& inputs,
      const std::vector<string>& outputs);

 private:
  caffe2::NetDef* net_;
};

} // namespace testing
} // namespace caffe2

#endif // CAFFE2_UTILS_TEST_UTILS_H_
