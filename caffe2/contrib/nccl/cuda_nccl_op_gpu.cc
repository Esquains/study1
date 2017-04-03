#include "caffe2/core/context_gpu.h"
#include "caffe2/core/operator.h"

#include "cuda_nccl_gpu.h"

namespace caffe2 {

nccl::NCCLExecution getNCCLElements(
    OperatorBase* op,
    const CUDAContext& context) {
  // We either do an N-N op, or an N-1 op.
  CAFFE_ENFORCE(
      op->def().input_size() == op->def().output_size() ||
      op->def().output_size() == 1);
  nccl::NCCLExecution ex;
  ex.stream_gpu_id = context.cuda_gpu_id();
  ex.stream = context.cuda_stream();
  ex.root = op->template GetSingleArgument<int>("root", 0);
  ex.elements.resize(op->def().input_size());
  for (auto i = 0; i < op->def().input_size(); ++i) {
    auto& el = ex.elements[i];
    el.src = &(op->Input<TensorCUDA>(i));
    if (i < op->def().output_size()) {
      el.dst = op->Output<TensorCUDA>(i);
    }
    // TODO - expensive (>1ms) - cache these.
    el.device = GetGPUIDForPointer(op->Input<TensorCUDA>(i).raw_data());
  }

  return ex;
}

namespace {
// Check if all inputs are float
template <typename T>
bool AllInputsAre(OperatorBase* op) {
  for (auto i = 0; i < op->def().input_size(); ++i) {
    if (op->Input<TensorCUDA>(i).IsType<T>()) {
      continue;
    } else {
      return false;
    }
  }
  return true;
}
}; // namespace

class NCCLAllreduceOp final : public Operator<CUDAContext> {
 public:
  using Operator::Operator;
  NCCLAllreduceOp(const OperatorDef& operator_def, Workspace* ws)
      : Operator<CUDAContext>(operator_def, ws) {}
  bool RunOnDevice() override {
    if (InputSize() == 1)
      return true;

    if (AllInputsAre<float>(this)) {
      nccl::NCCL<float>::AllReduce(getNCCLElements(this, context_));
      return true;
    } else if (AllInputsAre<float16>(this)) {
      nccl::NCCL<float16>::AllReduce(getNCCLElements(this, context_));
      return true;
    } else {
      return false;
    }
  }

 protected:
};

class NCCLBroadcastOp final : public Operator<CUDAContext> {
 public:
  USE_OPERATOR_FUNCTIONS(CUDAContext);
  using Operator::Operator;
  bool RunOnDevice() override {
    if (InputSize() == 1)
      return true;
    if (AllInputsAre<float>(this)) {
      nccl::NCCL<float>::Broadcast(getNCCLElements(this, context_));
      return true;
    } else if (AllInputsAre<float16>(this)) {
      nccl::NCCL<float16>::Broadcast(getNCCLElements(this, context_));
      return true;
    } else {
      return false;
    }
  }

 protected:
};

class NCCLReduceOp final : public Operator<CUDAContext> {
 public:
  USE_OPERATOR_FUNCTIONS(CUDAContext);
  using Operator::Operator;
  bool RunOnDevice() override {
    if (InputSize() == 1)
      return true;
    const auto& ex = getNCCLElements(this, context_);
    CAFFE_ENFORCE_EQ(
        ex.root, 0, "NCCLReduce has spurious deadlocks for non-zero root");

    if (AllInputsAre<float>(this)) {
      nccl::NCCL<float>::Reduce(ex);
      return true;
    } else if (AllInputsAre<float16>(this)) {
      nccl::NCCL<float16>::Reduce(ex);
      return true;
    } else {
      return false;
    }
  }

 protected:
};

class NCCLAllGatherOp final : public Operator<CUDAContext> {
 public:
  USE_OPERATOR_FUNCTIONS(CUDAContext);
  using Operator::Operator;
  bool RunOnDevice() override {
    if (InputSize() == 1)
      return true;
    if (AllInputsAre<float>(this)) {
      nccl::NCCL<float>::AllGather(getNCCLElements(this, context_));
      return true;
    } else if (AllInputsAre<float16>(this)) {
      nccl::NCCL<float16>::AllGather(getNCCLElements(this, context_));
      return true;
    } else {
      return false;
    }
  }

 protected:
};

namespace {
REGISTER_CUDA_OPERATOR(NCCLAllreduce, NCCLAllreduceOp);
OPERATOR_SCHEMA(NCCLAllreduce)
    .NumInputs(1, CAFFE2_COMPILE_TIME_MAX_GPUS)
    .NumOutputs(1, CAFFE2_COMPILE_TIME_MAX_GPUS)
    .AllowOneToOneInplace();
SHOULD_NOT_DO_GRADIENT(NCCLAllreduce);

REGISTER_CUDA_OPERATOR(NCCLBroadcast, NCCLBroadcastOp);
OPERATOR_SCHEMA(NCCLBroadcast)
    .NumInputs(1, CAFFE2_COMPILE_TIME_MAX_GPUS)
    .NumOutputs(1, CAFFE2_COMPILE_TIME_MAX_GPUS)
    .EnforceOneToOneInplace();
SHOULD_NOT_DO_GRADIENT(NCCLBroadcast);

REGISTER_CUDA_OPERATOR(NCCLReduce, NCCLReduceOp);
OPERATOR_SCHEMA(NCCLReduce)
    .NumInputs(1, CAFFE2_COMPILE_TIME_MAX_GPUS)
    .NumOutputs(1)
    .AllowInplace({{0, 0}});
SHOULD_NOT_DO_GRADIENT(NCCLReduce);

REGISTER_CUDA_OPERATOR(NCCLAllGather, NCCLAllGatherOp);
OPERATOR_SCHEMA(NCCLAllGather)
    .NumInputs(1, CAFFE2_COMPILE_TIME_MAX_GPUS)
    .NumOutputs(1, CAFFE2_COMPILE_TIME_MAX_GPUS);
SHOULD_NOT_DO_GRADIENT(NCCLAllGather);
} // namespace

} // namespace caffe2
