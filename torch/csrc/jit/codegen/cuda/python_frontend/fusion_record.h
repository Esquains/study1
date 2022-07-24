#pragma once
#include <c10/util/complex.h>
#include <torch/csrc/jit/codegen/cuda/arith.h>
#include <torch/csrc/jit/codegen/cuda/ops/normalization.h>
#include <torch/csrc/jit/codegen/cuda/python_frontend/fusion_definition.h>

namespace nvfuser {

struct RecordFunctor {
  RecordFunctor(std::vector<size_t> _args, std::vector<size_t> _outputs)
      : args(std::move(_args)), outputs(std::move(_outputs)) {}
  virtual ~RecordFunctor() = default;

  virtual void operator()(FusionDefinition& fd) = 0;

  std::vector<size_t> args;
  std::vector<size_t> outputs;
};

struct InputTensorRecord : RecordFunctor {
  InputTensorRecord(
      std::vector<size_t> _outputs,
      std::vector<int64_t> _symbolic_sizes,
      std::vector<bool> _contiguous_info,
      NvfDataType _dtype)
      : RecordFunctor({}, std::move(_outputs)),
        symbolic_sizes(std::move(_symbolic_sizes)),
        contiguous_info(std::move(_contiguous_info)),
        dtype(_dtype) {}
  ~InputTensorRecord() final = default;

  void operator()(FusionDefinition& fd) final {
    auto tv = TensorViewBuilder()
                  .ndims(symbolic_sizes.size())
                  .contiguity(contiguous_info)
                  .shape(symbolic_sizes)
                  .dtype(dtype)
                  .build();

    fd.setFusionState(outputs.at(0), tv);
    fd.addInput(tv);
  }

  std::vector<int64_t> symbolic_sizes;
  std::vector<bool> contiguous_info;
  NvfDataType dtype;
};

struct ScalarRecord : RecordFunctor {
  ScalarRecord(std::vector<size_t> _outputs, NvfDataType dtype)
      : RecordFunctor({}, std::move(_outputs)), dtype_(dtype) {}
  ~ScalarRecord() final = default;

  void operator()(FusionDefinition& fd) final {
    NvfVal* output = nullptr;
    if (dtype_ == NvfDataType::Double) {
      output = IrBuilder::create<torch::jit::fuser::cuda::Double>();
    } else if (dtype_ == NvfDataType::ComplexDouble) {
      output = IrBuilder::create<torch::jit::fuser::cuda::ComplexDouble>();
    } else if (dtype_ == NvfDataType::Bool) {
      output = IrBuilder::create<torch::jit::fuser::cuda::Bool>();
    } else if (dtype_ == NvfDataType::Int) {
      output = IrBuilder::create<torch::jit::fuser::cuda::Int>();
    } else {
      TORCH_CHECK(false, "Dtype is not supported:", dtype_);
    }
    fd.addInput(output);
    fd.setFusionState(outputs.at(0), output);
  }

 private:
  NvfDataType dtype_;
};

template <typename ExprType, typename ValueType>
struct ConstantRecord : RecordFunctor {
  ConstantRecord(std::vector<size_t> _outputs, ValueType val)
      : RecordFunctor({}, std::move(_outputs)), value_(val) {}
  ~ConstantRecord() final = default;

  void operator()(FusionDefinition& fd) final {
    NvfVal* output = IrBuilder::create<ExprType>(value_);
    fd.setFusionState(outputs.at(0), output);
  }

 private:
  ValueType value_;
};

template <class OutputType>
struct OutputRecord : RecordFunctor {
  OutputRecord(std::vector<size_t> _args)
      : RecordFunctor(std::move(_args), {}) {}
  ~OutputRecord() final = default;

  void operator()(FusionDefinition& fd) final {
    auto input = fd.getFusionState(args.at(0));

    // With C++17, this statement should be "if constexpr"
    if (std::is_same<OutputType, NvfTensorView>::value) {
      fd.addOutput(input->as<NvfTensorView>());
    } else {
      fd.addOutput(input);
    }
  }
};

struct BroadcastOpRecord : RecordFunctor {
  BroadcastOpRecord(
      std::vector<size_t> _args,
      std::vector<size_t> _outputs,
      std::vector<int64_t>& output_shape,
      std::vector<int64_t>& broadcast_dims)
      : RecordFunctor(std::move(_args), std::move(_outputs)),
        output_shape_(std::move(output_shape)),
        broadcast_dims_(std::move(broadcast_dims)) {}
  ~BroadcastOpRecord() final = default;

  void operator()(FusionDefinition& fd) final {
    auto arg = fd.getFusionState(args.at(0))->as<TensorView>();

    const auto arg_ndims = arg->domain()->noReductions().size();
    TORCH_CHECK(
        output_shape_.size() >= arg_ndims,
        "The new shape is expected to be greater-then-or-equal to the input",
        output_shape_.size(),
        arg_ndims);
    TORCH_CHECK(
        arg_ndims == broadcast_dims_.size(),
        "The broadcast dimensions should match the input dimensions.",
        arg_ndims,
        broadcast_dims_.size());

    std::vector<bool> is_broadcast_dim(output_shape_.size(), true);
    for (const auto idx : c10::irange(broadcast_dims_.size())) {
      if (idx > 0) {
        TORCH_CHECK(
            broadcast_dims_[idx - 1] < broadcast_dims_[idx],
            "Broadcast dimension is not greater than the previous value.");
      }
      TORCH_CHECK(
          broadcast_dims_[idx] < static_cast<int>(output_shape_.size()),
          "Invalid broadcast_dims value.");
      is_broadcast_dim.at(broadcast_dims_[idx]) = false;
    }

    auto output = torch::jit::fuser::cuda::broadcast(arg, is_broadcast_dim);
    fd.setFusionState(outputs.at(0), output);
  }

 private:
  std::vector<int64_t> output_shape_;
  std::vector<int64_t> broadcast_dims_;
};

template <class OutType, class ArgType>
struct CastOpRecord : RecordFunctor {
  CastOpRecord(
      std::vector<size_t> _args,
      std::vector<size_t> _outputs,
      std::function<OutType(NvfDataType, ArgType)> fusion_op,
      NvfDataType dtype)
      : RecordFunctor(std::move(_args), std::move(_outputs)),
        fusion_op_(fusion_op),
        dtype_(dtype) {}
  ~CastOpRecord() final = default;

  void operator()(FusionDefinition& fd) final {
    auto arg = dynamic_cast<ArgType>(fd.getFusionState(args.at(0)));
    auto output = fusion_op_(dtype_, arg);
    fd.setFusionState(outputs.at(0), output);
  }

 private:
  std::function<OutType(NvfDataType, ArgType)> fusion_op_;
  NvfDataType dtype_;
};

template <class OutType, class... ArgTypes>
struct OpRecord : RecordFunctor {
  OpRecord(
      std::vector<size_t> _args,
      std::vector<size_t> _outputs,
      std::function<OutType(ArgTypes...)> fusion_op)
      : RecordFunctor(std::move(_args), std::move(_outputs)),
        fusion_op_(fusion_op) {}
  ~OpRecord() final = default;

  template <class TupleType, std::size_t... Is>
  OutType opFunc(
      FusionDefinition& fd,
      TupleType& tp,
      std::index_sequence<Is...>) {
    return fusion_op_(
        dynamic_cast<typename std::tuple_element<Is, TupleType>::type>(
            fd.getFusionState(args.at(Is)))...);
  }

  void operator()(FusionDefinition& fd) final {
    using arg_tuple_t = std::tuple<ArgTypes...>;
    auto indices =
        std::make_index_sequence<std::tuple_size<arg_tuple_t>::value>();
    arg_tuple_t inputs;
    auto output = opFunc(fd, inputs, indices);
    fd.setFusionState(outputs.at(0), output);
  }

 private:
  std::function<OutType(ArgTypes...)> fusion_op_;
};

struct ReductionOpRecord : RecordFunctor {
  ReductionOpRecord(
      std::vector<size_t> _args,
      std::vector<size_t> _outputs,
      std::function<
          NvfTensorView*(NvfTensorView*, std::vector<int>&, bool, NvfDataType)>
          fusion_op,
      std::vector<int> axes,
      bool keep_dim,
      NvfDataType dtype)
      : RecordFunctor(std::move(_args), std::move(_outputs)),
        fusion_op_(fusion_op),
        axes_(std::move(axes)),
        keep_dim_(keep_dim),
        dtype_(dtype) {}
  ~ReductionOpRecord() final = default;

  void operator()(FusionDefinition& fd) final {
    auto arg = fd.getFusionState(args.at(0))->as<NvfTensorView>();
    auto output = fusion_op_(arg, axes_, keep_dim_, dtype_);
    fd.setFusionState(outputs.at(0), output);
  }

 private:
  std::function<
      NvfTensorView*(NvfTensorView*, std::vector<int>&, bool, NvfDataType)>
      fusion_op_;
  std::vector<int> axes_;
  bool keep_dim_;
  NvfDataType dtype_;
};

struct VarianceOpRecord : RecordFunctor {
  VarianceOpRecord(
      std::vector<size_t> _args,
      std::vector<size_t> _outputs,
      std::vector<int>& axes,
      int64_t correction,
      bool keep_dim)
      : RecordFunctor(std::move(_args), std::move(_outputs)),
        axes_(axes),
        correction_(correction),
        keep_dim_(keep_dim) {}
  ~VarianceOpRecord() final = default;

  void operator()(FusionDefinition& fd) final {
    auto arg = fd.getFusionState(args.at(0))->as<NvfTensorView>();
    auto output =
        torch::jit::fuser::cuda::variance(arg, axes_, correction_, keep_dim_);
    fd.setFusionState(outputs.at(0), output);
  }

 private:
  std::vector<int> axes_;
  int64_t correction_;
  bool keep_dim_;
};

} // namespace nvfuser
