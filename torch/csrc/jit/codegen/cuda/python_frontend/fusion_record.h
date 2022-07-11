#pragma once
#include <torch/csrc/jit/codegen/cuda/python_frontend/fusion_definition.h>

namespace nvfuser {
struct RecordFunctor {
  RecordFunctor(std::vector<size_t> _args, std::vector<size_t> _outputs) :
    args(std::move(_args)),
    outputs(std::move(_outputs)) {}
  virtual void operator()(FusionDefinition& fd) = 0;

  std::vector<size_t> args;
  std::vector<size_t> outputs;
};

struct InputTensorRecord : RecordFunctor {
  InputTensorRecord(std::vector<size_t> _outputs, 
                    std::vector<int64_t> _symbolic_sizes,
                    std::vector<bool> _contiguous_info,
                    NvfDataType _dtype):
    RecordFunctor({}, std::move(_outputs)),
    symbolic_sizes(std::move(_symbolic_sizes)),
    contiguous_info(std::move(_contiguous_info)),
    dtype(_dtype) {}
  void operator()(FusionDefinition &fd) final {
    auto tv = TensorViewBuilder()
                  .ndims(symbolic_sizes.size())
                  .contiguity(contiguous_info)
                  .shape(symbolic_sizes)
                  .dtype(dtype)
                  .build();
    
    fd.fusion_state.at(outputs.at(0)) = tv;
    fd.addInput(tv);
  }

  std::vector<int64_t> symbolic_sizes;
  std::vector<bool> contiguous_info;
  NvfDataType dtype;
};

template<class OutputType>
struct OutputRecord : RecordFunctor {
  OutputRecord(std::vector<size_t> _args):
    RecordFunctor(std::move(_args), {}) {}

  void operator()(FusionDefinition &fd) final {
    auto input = fd.fusion_state.at(args.at(0));

    // With C++17, this statement should be "if constexpr" 
    if (std::is_same<OutputType, NvfTensorView>::value) {
      fd.addOutput(input->as<NvfTensorView>());
    } else {
      fd.addOutput(input);
    }
  }
};

template<class OutType, class... ArgTypes>
struct OpRecord : RecordFunctor {
  OpRecord(std::vector<size_t> _args,
           std::vector<size_t> _outputs,
           std::function<OutType(ArgTypes...)> fusion_op) :
    RecordFunctor(std::move(_args), std::move(_outputs)),
    fusion_op_(fusion_op) {}

  template<class TupleType, std::size_t... Is>
  OutType opFunc(FusionDefinition& fd, TupleType& tp, std::index_sequence<Is...>) {
    return fusion_op_(dynamic_cast<typename std::tuple_element<Is, TupleType>::type>(fd.fusion_state.at(args.at(Is))) ...);
  }

  void operator()(FusionDefinition& fd) final {
    using arg_tuple_t = std::tuple<ArgTypes...>;
    auto indices= std::make_index_sequence<std::tuple_size<arg_tuple_t>::value>();
    arg_tuple_t inputs;
    auto output = opFunc(fd, inputs, indices);
    fd.fusion_state.at(outputs.at(0)) = output;
  }

 private:
  std::function<OutType(ArgTypes...)> fusion_op_;
};

} // nvfuser namespace
