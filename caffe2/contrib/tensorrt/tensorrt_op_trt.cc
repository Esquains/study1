#include "caffe2/contrib/tensorrt/tensorrt_op_trt.h"
#include "caffe2/core/logging.h"

#include <numeric>
#include <unordered_map>

namespace caffe2 {

namespace {
// Note that input of trt tensor is in CHW format, while our tensor is NCHW
// \return -1 if there is dimension mismatch between C2 tensor and trt tensor.
// Otherwise, return the product of CHW dimensions
int64_t CheckDims(
    const nvinfer1::Dims& nv_dims,
    const std::vector<TIndex>& c2_dims) {
  if (nv_dims.nbDims + 1 != c2_dims.size()) {
    CAFFE_THROW("Mismatched dimensions between TRT input and C2 input");
  }
  int64_t chw = 1;
  for (int i = 0; i < nv_dims.nbDims; ++i) {
    if (nv_dims.d[i] != c2_dims[i + 1]) {
      return -1;
    }
    chw *= nv_dims.d[i];
  }
  return chw;
}

} // namespace

// Upon construction, we build the inference engine by deserializing from
// protobuf string. And since we know the input/output blobs, we can do the
// binding here too.
TensorRTOp::TensorRTOp(const OperatorDef& operator_def, Workspace* ws)
    : Operator<CUDAContext>(operator_def, ws),
      logger_((nvinfer1::ILogger::Severity)(
          OperatorBase::GetSingleArgument<int>("log_verbosity", FLAGS_minloglevel))),
      max_batch_size_(OperatorBase::GetSingleArgument<int>("max_batch_size", 1)) {
  {
    auto engine_string =
        OperatorBase::GetSingleArgument<std::string>("serialized_engine", "");
    CAFFE_ENFORCE(!engine_string.empty(), "Empty serialized TensorRT engine!");
    auto trt_runtime = InferObject(nvinfer1::createInferRuntime(logger_));
    // TODO(support trt plugin factory)
    trt_engine_ = InferObject(trt_runtime->deserializeCudaEngine(
        engine_string.data(), engine_string.size(), nullptr));
  }

  CAFFE_ENFORCE(trt_engine_, "Cannot deserialize TensorRT engine!");

  // match and bind the input/output
  const int num_bindings = trt_engine_->getNbBindings();
  int output_idx = 0;
  for (int b = 0; b < num_bindings; ++b) {
    nv_dims_.push_back(trt_engine_->getBindingDimensions(b));
    bool is_input = trt_engine_->bindingIsInput(b);
    is_input_.push_back(is_input);
    if (!is_input) {
      // For output, we try to get its output size hint
      const std::string key = MakeString("output_size_hint_", output_idx);
      auto output_size_hint = OperatorBase::GetRepeatedArgument<int>(key);
      if (!output_size_hint.empty()) {
        std::vector<TIndex> dims;
        for (const auto v: output_size_hint) {
          dims.push_back(v);
        }
        output_size_hints_.emplace(output_idx, std::move(dims));
      }
      ++output_idx;
    }
  }

  trt_executor_ = InferObject(trt_engine_->createExecutionContext());
}

void TensorRTOp::MaybeAdjustOutputShape(int output_idx, std::vector<TIndex>* dims) {
  const auto it = output_size_hints_.find(output_idx);
  if (it != output_size_hints_.end()) {
    const auto& dims_hint = it->second;
    auto total_trt = std::accumulate(dims->begin(), dims->end(), (TIndex)(1), std::multiplies<TIndex>());
    auto total_c2 = std::accumulate(dims_hint.begin(), dims_hint.end(), (TIndex)(1), std::multiplies<TIndex>());
    if (total_c2 != total_trt) {
      LOG(WARNING) << "The total size of TensorRT op output and hint don't match: " << total_trt << " vs " << total_c2;
      return;
    }

    bool identical_shape = std::equal(dims->cbegin(), dims->cend(), dims_hint.cbegin());
    // We conform to the output shape hints. NB: We might need an explicit reshape op for this
    if (!identical_shape) {
      *dims = dims_hint;
    }
  }
}

bool TensorRTOp::RunOnDevice() {
  CAFFE_ENFORCE(trt_executor_);
  // Decide input batch size
  size_t N = 0;
  for (int i = 0; i < InputSize(); ++i) {
    const auto& input_tensor = Input(i);
    const auto& tensor_dims = input_tensor.dims();
    if (i == 0 && !tensor_dims.empty()) {
      N = tensor_dims.front();
    } else {
      CAFFE_ENFORCE_EQ(
          N, tensor_dims.front(), "Mismatched batch size in input tensors");
    }
  }

  // Input size check and output allocation
  for (auto i = 0; i < is_input_.size(); ++i) {
    const auto& dims = nv_dims_[i];
    if (is_input_[i]) {
      // Check input dimensions

    }
  }

  // We need to do the binding at RunOnDevice time because we only know the
  // exact shapes of the tensors now. In addtion, since TensorRT engine has
  // max_batch_size, we need to call that multiple times if input batch size
  // exceeeds this limit.
  CAFFE_ENFORCE_EQ(is_input_.size(), nv_dims_.size());
  std::vector<void*> bindings;
  bindings.reserve(is_input_.size());
  auto batch_size = max_batch_size_;
  for (size_t offset = 0; offset < N; offset += batch_size) {
    bindings.clear();
    batch_size =
        offset + max_batch_size_ > N ? N - offset : max_batch_size_;
    VLOG(2) << "Offset: " << offset << ", batch_size: " << batch_size << ", N: " << N;
    int input_idx = 0;
    int output_idx = 0;
    for (auto i = 0; i < is_input_.size(); ++i) {
      const auto& dims = nv_dims_[i];
      if (is_input_[i]) {
        // input, check input dimensions
        const auto& input_tensor = Input(input_idx++);
        const float* input_data = input_tensor.data<float>();
        const auto& tensor_dims = input_tensor.dims();
        auto chw = CheckDims(dims, tensor_dims);
        bindings.push_back((void*)(input_data + offset * chw));
      } else {
        // output, we need to allocate the output tensor at first batch run
        auto* output_tensor = Output(output_idx);
        std::vector<TIndex> tensor_dims;
        tensor_dims.push_back(N);
        int64_t chw = 1;
        for (int i = 0; i < dims.nbDims; ++i) {
          tensor_dims.push_back(dims.d[i]);
          chw *= dims.d[i];
        }

        if (offset == 0) {
          MaybeAdjustOutputShape(output_idx, &tensor_dims);
          output_tensor->Resize(tensor_dims);
        }
        ++output_idx;
        float* output_data = output_tensor->mutable_data<float>();
        bindings.push_back((void*)(output_data + offset * chw));
      }
    }

    CAFFE_ENFORCE_EQ(bindings.size(), InputSize() + OutputSize());
    if(!trt_executor_->execute(batch_size, bindings.data())){
      CAFFE_THROW("Error running the TensorRT executor");
    }
  }
  return true;
}

OPERATOR_SCHEMA(TensorRT)
    .NumInputs(0, INT_MAX)
    .NumOutputs(0, INT_MAX)
    .SetDoc(R"DOC(
The TensorRT operator is a black-box operator serialized from prebuilt TensorRT
Engine string. It will take the input, do the computation by calling TensorRT
inference engine and generate the outputs.

This is a GPU only operator.
)DOC")
    .Arg(
        "log_verbosity",
        "(int default 0) verbosity of the TensorRt engine log."
        )
    .Arg(
        "serialized_engine",
        "(string default=\"\" blob for serialized TensorRT engine."
        "Note that serialized engine is not compatible across platform and "
        "different TensorRT version."
        )
    .Arg(
        "batch_size",
        "(int default 0) Batch size set by the TensorRT engine builder."
        "It must be no larger than the max_batch_size of the engine builder so "
        "it is better not to edit this manually.");

REGISTER_CUDA_OPERATOR(TensorRT, TensorRTOp);
} // namespace caffe2
