#include <torch/csrc/jit/passes/onnx/constant_fold.h>
#include <c10/util/Exception.h>

#include <c10/util/Optional.h>
#include <algorithm> 

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

namespace torch {
namespace jit {

namespace onnx {
using namespace ::c10::onnx;
}

using ParamMap = std::map<std::string, at::Tensor>;
using ValueToParamPairMap = std::map<Value*, std::pair<std::string, at::Tensor>>;

static ValueToParamPairMap buildValueToParamsMap(Block* b, const ParamMap& paramsDict) {
  ValueToParamPairMap valsToParamsMap;
  for(auto& input : b->inputs()) {
      auto it = paramsDict.find(input->uniqueName());
      if (it != paramsDict.end()) {
          valsToParamsMap[input] = *it;
      }
  }
  return valsToParamsMap;
}

static void buildParamsMapFromValueToParamsMap(
    const ValueToParamPairMap& valsToParamsMap, ParamMap& paramsDict) {
  paramsDict.clear();
  for(auto& nameTensorParamPair : valsToParamsMap) {
    paramsDict.insert(nameTensorParamPair.second);
  }
}

static void eraseUnusedNodeOutputs(Node* node) {
  std::vector<std::string> removedOutputNames;
  for (size_t i_1 = node->outputs().size(); i_1 > 0; --i_1) {
      size_t i = i_1 - 1;
      if (!node->outputs().at(i)->hasUses()) {
        node->eraseOutput(i);
      }
  }
}

static at::Tensor runTorchBackendForOnnx(const Node* node, std::vector<at::Tensor>& inputTensorValues) {
  at::Tensor updated_val;
  if (node->kind() == onnx::Slice) {
    assert(inputTensorValues.size() == 1);
    if ( !(node->hasAttributeS("axes") && node->hasAttributeS("starts") && node->hasAttributeS("ends")) ) {
      throw std::runtime_error("Missing attribute(s) in onnx::Slice op.");
    }
    auto axesAttr = node->is(attr::axes);
    auto startsAttr = node->is(attr::starts);
    auto endsAttr = node->is(attr::ends);
    if (axesAttr.size() != startsAttr.size() || axesAttr.size() != endsAttr.size()) {
      throw std::runtime_error("onnx::Slice node attributues named, axes, starts, and ends, must be the same length.");
    }
    updated_val = inputTensorValues[0];
    for (size_t i = 0; i < axesAttr.size(); ++i) {
      updated_val = at::narrow(updated_val, axesAttr[i], startsAttr[i], endsAttr[i] - startsAttr[i]);
    }
  }  
  else if (node->kind() == onnx::Concat) {
    updated_val = at::cat(at::TensorList(inputTensorValues), node->i(attr::axis));
  }
  else if (node->kind() == onnx::Unsqueeze) {
    assert(inputTensorValues.size() == 1);
    if (!node->hasAttributeS("axes")) {
      throw std::runtime_error("Missing attribute 'axes' in onnx::Unsqueeze op.");
    }
    updated_val = inputTensorValues[0];
    for (auto axis: node->is(attr::axes)) {
      updated_val = at::unsqueeze(updated_val, axis);
    }
  }
  else if (node->kind() == onnx::Transpose) {
    assert(inputTensorValues.size() == 1);
    if (!node->hasAttributeS("perm")) {
      throw std::runtime_error("Missing attribute 'perm' in onnx::Transpose op.");
    }
    updated_val = inputTensorValues[0].permute(node->is(attr::perm));
  }
  else {
    updated_val = at::empty({0});
    auto qe = updated_val.size(0);
  }
  return updated_val;
}

static bool isConstant(Value* val, const ValueToParamPairMap& valsToParamsMap) {
  auto parentNode = val->node();
  return (parentNode->kind() == prim::Param && 
          valsToParamsMap.find(val) != valsToParamsMap.end()) || // Checks val is a parameter and not a real input
         (parentNode->kind() == onnx::Constant && !parentNode->mustBeNone() &&
          parentNode->kindOf(attr::value) == AttributeKind::t); // Check other types?
}

static std::vector<at::Tensor> getValues(Node* node, const ValueToParamPairMap& valsToParamsMap) {
  size_t numInputs = node->inputs().size();
  std::vector<at::Tensor> inputTensorValues;
  inputTensorValues.reserve(numInputs);
  for (auto val : node->inputs()) {
    if (val->node()->kind() == prim::Param) {
      auto itr = valsToParamsMap.find(val);
      if(itr == valsToParamsMap.end()) {
        throw std::runtime_error("getValues: Input value not found amongst constant parameters.");
      }
      inputTensorValues.push_back(itr->second.second);
    }
    else if (val->node()->kind() == onnx::Constant) {
      inputTensorValues.push_back(val->node()->t(attr::value));
    }
    else {
      throw std::runtime_error("getValues: Unsupported kind of constant node found.");
    }
  }
  AT_ASSERT(inputTensorValues.size() == numInputs);
  return inputTensorValues;
}

static void eraseUnusedValuesFromMap(ValueToParamPairMap& valsToParamsMap) {
  auto it = valsToParamsMap.begin();
  while (it != valsToParamsMap.end()) {
    if (!it->first->hasUses()) {
      it = valsToParamsMap.erase(it);
    } 
    else {
      ++it;
    }
  }
}

// This method updates the block in-place to fold all the one-time 
// constant-based computations/ops into an initializer node.
void ConstantFoldONNX(Block* b, ParamMap& paramsDict) {
  auto sourceNode = b->param_node();
  AT_ASSERT(sourceNode);
  auto valsToParamsMap = buildValueToParamsMap(b, paramsDict);
  // Only the root block is constant-folded. Folding nested blocks is
  // not supported for now.
  for (auto it = b->nodes().begin(), end = b->nodes().end(); it != end; ++it) {
    auto node = *it;
    if (node->outputs().size() > 1) {
        // Constant folding for multiple-output nodes not supported. Skip it.
        continue;
      }
    if (!std::all_of(node->inputs().begin(), node->inputs().end(),
        [&valsToParamsMap](Value* v) { return isConstant(v, valsToParamsMap); })) {
        // If all the inputs to this node are not either parameter or
        // onnx::Constant, then skip this node.
        continue;
    }
    auto inputTensorValues = getValues(node, valsToParamsMap);
    if (inputTensorValues.empty()) {
        // This is a terminal node with no inputs, such as onnx::Constant. Skip it.
        continue;
    }
    auto updated_val = runTorchBackendForOnnx(node, inputTensorValues);
    if (updated_val.size(0) == 0) {
        // Constant folding is not supported for this op. Skip it.
        continue;
      }
    // Create a new source node output value. Add a corresponding entry
    // in valToParamMap. Replace the downstream inputs with this value, 
    // and disconnect all the input values of the folded node.
    auto newSourceNodeOutput = sourceNode->addOutput();
    valsToParamsMap.insert({newSourceNodeOutput, 
                            std::make_pair(newSourceNodeOutput->uniqueName(), updated_val)});
    newSourceNodeOutput->inferTypeFrom(updated_val);
    node->outputs().at(0)->replaceAllUsesWith(newSourceNodeOutput);

    node->removeAllInputs();
  }
  eraseUnusedValuesFromMap(valsToParamsMap);
  eraseUnusedNodeOutputs(sourceNode);
  buildParamsMapFromValueToParamsMap(valsToParamsMap, paramsDict);
  return;
}

} // namespace jit
} // namespace torch