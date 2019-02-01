#include "caffe2/opt/optimize_ideep.h"
#include "caffe2/opt/converter.h"
#include "caffe2/opt/fusion.h"

#ifdef CAFFE2_USE_MKLDNN
#include "caffe2/ideep/ideep_utils.h"
#endif

namespace caffe2 {
namespace opt {

using namespace nom;

#ifndef CAFFE2_USE_MKLDNN
void OptimizeForIdeep(
    repr::NNModule* nn,
    caffe2::Workspace* ws,
    bool training_mode) {
  LOG(WARNING) << "Only support optimizations for IDEEP";
}

#else
USE_IDEEP_DEF_ALIASES();

Blob* getBlob(const std::string name, caffe2::Workspace* ws) {
  CAFFE_ENFORCE(ws->HasBlob(name), "Blob ", name, " not in workspace");
  return ws->GetBlob(name);
}

Blob* getBlob(repr::NNGraph::NodeRef node, caffe2::Workspace* ws) {
  auto tensor = repr::nn::get<repr::Tensor>(node);
  CAFFE_ENFORCE(ws->HasBlob(tensor->getName()), "Blob not in workspace");
  return getBlob(tensor->getName(), ws);
}

template <class T>
T *getMutableTensor(Blob *blob) {
  CAFFE_ENFORCE(blob, "Blob is invalid");
  if (blob && blob->template IsType<T>()) {
    return blob->template GetMutable<T>();
  }
  return nullptr;
}

const caffe2::OperatorDef& getOpDef(const repr::NeuralNetOperator& nnOp) {
  auto annotation = nnOp.getAnnotation();
  if (annotation == nullptr) {
    CAFFE_THROW("Cannot get Operator annotation");
  }
  return dyn_cast<Caffe2Annotation>(annotation)->getOperatorDef();
}

caffe2::OperatorDef* getMutableOpDef(repr::NeuralNetOperator& nnOp) {
  auto annotation = nnOp.getMutableAnnotation();
  if (annotation == nullptr) {
    CAFFE_THROW("Cannot get Operator annotation");
  }
  return dyn_cast<Caffe2Annotation>(annotation)->getMutableOperatorDef();
}

bool isOnIdeepDevice(const repr::NeuralNetOperator &nnOp) {
  // We only want to fuse for IDEEP convs
  const auto &op = getOpDef(nnOp);
  return op.device_option().device_type() == DeviceTypeProto::PROTO_IDEEP;
}

bool isOpType(const repr::NNGraph::NodeRef& nodeRef, string typeName) {
  if (!repr::nn::is<repr::NeuralNetOperator>(nodeRef)) {
    return false;
  }
  auto op = repr::nn::get<repr::NeuralNetOperator>(nodeRef);
  auto opDef = getOpDef(*op);
  return opDef.type() == typeName;
}

bool isConvFusion(repr::NNGraph::NodeRef convNode, int fusion_type) {
  auto conv = repr::nn::get<repr::Conv>(convNode);
  auto &op = getOpDef(*conv);

  if (op.type() == "ConvFusion") {
    for (const auto &arg : op.arg()) {
      if (arg.name() == "fusion_type") {
        return arg.i() == fusion_type;
      }
    }
  }

  return false;
}

bool shouldFuseConv(const repr::Conv& conv) {
  return isOnIdeepDevice(conv) ? (conv.getGroup() <= 1) : false;
}

void removeStopGradientForInference(repr::NNModule *nn) {
  auto allNodes = nn->dataFlow.getMutableNodes();
  for (int i = 0; i < allNodes.size(); ++i) {
    auto node = allNodes[i];
    if (!isOpType(node, "StopGradient")) {
      continue;
    }

    auto stopGradInput = repr::nn::getInputs(node).front();
    auto stopGradOutput = repr::nn::getOutputs(node).front();
    auto inputName = repr::nn::get<repr::Tensor>(stopGradInput)->getName();
    auto outputName = repr::nn::get<repr::Tensor>(stopGradOutput)->getName();
    if (inputName == outputName) {
      nn->dataFlow.replaceNode(stopGradOutput, stopGradInput);
      nn->dataFlow.deleteNode(node);
    }
  }
}

void resetConvForFusion(repr::NNGraph::NodeRef convNode, int fusion_type) {
  auto conv = repr::nn::get<repr::Conv>(convNode);
  auto* op = getMutableOpDef(*conv);
  if (op == nullptr) {
    return;
  }

  if (op->type() == "ConvFusion") {
    CAFFE_ENFORCE(fusion_type == FUSION_CONV_RELU, "Invalid nest fusion");
    for (auto& arg : *op->mutable_arg()) {
      if (arg.name() == "fusion_type") {
        CAFFE_ENFORCE(arg.i() == FUSION_CONV_SUM, "Invalid nest fusion");
        arg.set_i(FUSION_CONV_SUM_RELU);
        return;
      }
    }
    CAFFE_THROW("Can not find fusion type in ConvFusion");
  }

  CAFFE_ENFORCE(fusion_type < FUSION_CONV_SUM_RELU, "Invalid fusion type");
  op->set_type("ConvFusion");
  auto* arg = op->add_arg();
  arg->set_name("fusion_type");
  arg->set_i(fusion_type);
}

void removeArg(repr::NeuralNetOperator &nnOp, std::string argName) {
  auto *op = getMutableOpDef(nnOp);
  auto &opArgs = *op->mutable_arg();
  auto remove_arg = [](decltype(opArgs) &args, std::string &name) {
    for (auto it = args.begin(); it != args.end(); it ++) {
      if (it->name() == name) {
        args.erase(it);
        return true;
      }
    }
    return false;
  };
  while(remove_arg(opArgs, argName));
}

void moveOpArg(caffe2::Workspace *ws, std::string argName,
    repr::NeuralNetOperator *srcOp, repr::NeuralNetOperator *dstOp) {
  if (argName.empty()
      || srcOp == nullptr
      || dstOp == nullptr
      || srcOp == dstOp)
    return;
  removeArg(*dstOp, argName);

  auto &src = getOpDef(*srcOp);
  auto &src_args = src.arg();
  auto src_it = src_args.begin();
  for (; src_it != src_args.end(); src_it ++) {
    if (src_it->name() == argName)
      break;
  }
  if (src_it == src_args.end())
    return;

  auto *dst = getMutableOpDef(*dstOp);
  auto *arg = dst->add_arg();
  *arg = *src_it;
  arg->set_name(argName);
}

bool fuseConvBNAndAffChHelperForIdeep(repr::NNModule* nn, caffe2::Workspace* ws) {
  for (auto node_pair : repr::nn::dataIterator<repr::Conv>(nn->dataFlow)) {
    bool no_bias = false;
    repr::NNGraph::NodeRef convNode;
    repr::Conv* conv;
    std::tie(conv, convNode) = node_pair;

    if (!isOnIdeepDevice(*conv)) {
      LOG(WARNING) << "Not a IDEEP operator";
      continue;
    }

    const auto& convOp = getOpDef(*conv);
    if (convOp.type() == "ConvFusion") {
      continue;
    }

    auto convOutput = repr::nn::getOutputs(convNode).front();
    auto consumers = repr::nn::getConsumers(convOutput);
    // convOutput is NOT referenced by sequential ops after BN.
    if (consumers.size() != 1) {
      continue;
    }

    bool isBN;
    auto consumer = consumers.front();
    if (repr::nn::is<repr::BatchNormalization>(consumer)) {
      isBN = true;
    } else if (isOpType(consumer, "AffineChannel")) {
      isBN = false;
    } else {
      continue;
    }

    auto bnOrAffChNode = consumer;
    auto bn = isBN ? repr::nn::get<repr::BatchNormalization>(bnOrAffChNode) : nullptr;
    auto bnOrAffChOutput = repr::nn::getOutputs(bnOrAffChNode).front();

    auto convInputs = repr::nn::getInputs(convNode);
    if (convInputs.size() < 2) {
      LOG(WARNING) << "Invalid convolution input size";
      continue;
    }

    auto bnOrAffChInputs = repr::nn::getInputs(bnOrAffChNode);
    int numInputs = isBN ? 5 : 3;
    if (bnOrAffChInputs.size() < numInputs) {
      LOG(WARNING) << "Invalid input size: "
                   << bnOrAffChInputs.size()
                   << ", expect " << numInputs;
      continue;
    }

    // When no bias, borrow BN bias
    if (convInputs.size() < 3) {
      no_bias = true;
      nn->dataFlow.createEdge(bnOrAffChInputs[2], convNode);
      convInputs = repr::nn::getInputs(convNode);
    }

#define EXPOSE_TENSOR_DATA(name, index, nodes, need_init)                \
  itensor* name = nullptr;                                               \
  itensor name##Tensor;                                                  \
  float* name##Data = nullptr;                                           \
  if (need_init) {                                                       \
    name = getMutableTensor<itensor>(getBlob(nodes[index], ws));         \
    if (name == nullptr) {                                               \
      LOG(WARNING) << #name " not a IDEEP tensor";                       \
      continue;                                                          \
    }                                                                    \
    name##Tensor.resize(name->get_dims(), name->get_data_type());        \
    name##Tensor.feed_from(*name);                                       \
    CAFFE_ENFORCE(                                                       \
      name##Tensor.is_public_format(), #name " not with public format"); \
    name##Data = static_cast<float*>(name##Tensor.get_data_handle());    \
  }

    EXPOSE_TENSOR_DATA(filter, 1, convInputs, true);
    EXPOSE_TENSOR_DATA(biasConv, 2, convInputs, true);

    EXPOSE_TENSOR_DATA(scale, 1, bnOrAffChInputs, true);
    EXPOSE_TENSOR_DATA(biasBNOrAffCh, 2, bnOrAffChInputs, true);
    EXPOSE_TENSOR_DATA(mean, 3, bnOrAffChInputs, isBN);
    EXPOSE_TENSOR_DATA(variance, 4, bnOrAffChInputs, isBN);

#undef EXPOSE_TENSOR_DATA

    // Assume M{CHW,HWC}
    auto chwDim = filterTensor.get_dim(1) * filterTensor.get_dim(2) *
        filterTensor.get_dim(3);
    for (auto c = 0; c < filterTensor.get_dim(0); ++c) {
      float mean_val = 0;
      float variance_val = 1;
      if (isBN) {
        mean_val = meanData[c];
        variance_val = std::sqrt(varianceData[c] + bn->getEpsilon());
      }
      float coeff = scaleData[c] / variance_val;
      for (auto i = 0; i < chwDim; ++i) {
        filterData[c * chwDim + i] *= coeff;
      }

      if (no_bias) {
        biasConvData[c] = biasBNOrAffChData[c] - mean_val * coeff;
      } else {
        biasConvData[c] =
          biasBNOrAffChData[c] + (biasConvData[c] - mean_val) * coeff;
      }
    }

    filter->feed_from(filterTensor);
    biasConv->feed_from(biasConvTensor);
    nn->dataFlow.replaceNode(convOutput, bnOrAffChOutput);

    nn->dataFlow.deleteNode(bnOrAffChNode);
    nn->dataFlow.deleteNode(convOutput);

    return true;
  }

  return false;
}

void fuseConvBNAndAffChForIdeep(repr::NNModule* nn, caffe2::Workspace* ws) {
  while (fuseConvBNAndAffChHelperForIdeep(nn, ws)) {}
}

void fuseConvSumForIdeep(repr::NNModule* nn, caffe2::Workspace* ws) {
  // Assume the order of nodes from getMutableNodes conforms to
  // the original topo order of operators
  auto allNodes = nn->dataFlow.getMutableNodes();
  for (int i = allNodes.size() - 1; i > 0; i--) {
    auto sumNode = allNodes[i];
    if (!repr::nn::hasInputs(sumNode)) {
      continue;
    }

    // [Caution] on IDEEP device, only element-wise Add operator is
    // supported yet. It totally works as element-wise sum without scalar broadcast.
    bool is_dnnlowp_sum = false;
    if (isOpType(sumNode, "Int8Sum")
        || isOpType(sumNode, "Int8SumRelu")
        || isOpType(sumNode, "Int8AddRelu")) {
      is_dnnlowp_sum = true;
    } else if (!repr::nn::is<repr::Sum>(sumNode)
        && !isOpType(sumNode, "Add")) {
      continue;
    }

    auto sum = repr::nn::get<repr::NeuralNetOperator>(sumNode);
    if (!isOnIdeepDevice(*sum)) {
      LOG(WARNING) << "Not a IDEEP operator";
      continue;
    }

    auto sumInputs = repr::nn::getInputs(sumNode);
    if (sumInputs.size() != 2) {
      continue;
    }

    bool should_fuse = true;
    for (auto input : sumInputs) {
      auto consumer = repr::nn::getConsumers(input).back();
      if (consumer != sumNode) {
        should_fuse = false;
        break;
      }
    }
    // Sum inputs should not be referenced by sequential ops.
    if (!should_fuse) {
      continue;
    }

    repr::NNGraph::NodeRef convNode = nullptr;
    while (--i >= 0) {
      if (!repr::nn::hasInputs(sumNode)) {
        continue;
      }

      // Find the nearest Op before Sum
      if (repr::nn::is<repr::NeuralNetOperator>(allNodes[i])) {
        // The Op must be a Conv
        if (repr::nn::is<repr::Conv>(allNodes[i])
            || isOpType(allNodes[i], "Int8Conv")) {
          convNode = allNodes[i];
        }
        break;
      }
    }
    if (convNode == nullptr) {
      continue;
    }

    auto conv = repr::nn::get<repr::NeuralNetOperator>(convNode);
    if (!isOnIdeepDevice(*conv)) {
      LOG(WARNING) << "Not a IDEEP operator";
      continue;
    }

    auto group = 1;
    auto *convOp = getMutableOpDef(*conv);
    for (const auto &arg : convOp->arg()) {
      if (arg.name() == "group") {
        group = arg.i();
        break;
      }
    }
    if (group > 1) {
      LOG(WARNING) << "Not support conv sum fusion with grouped filter";
      continue;
    }

    auto convOutput = repr::nn::getOutputs(convNode).front();
    repr::NNGraph::NodeRef sumInputX =
        (sumInputs[0] == convOutput ? sumInputs[1] : sumInputs[0]);
    CAFFE_ENFORCE(sumInputX != nullptr, "Invalid sum inputs");
    if (sumInputX->getInEdges().size() <= 0) {
      continue;
    }

    auto preNode = repr::nn::getProducer(sumInputX);
    if (preNode == nullptr ||
        !repr::nn::is<repr::NeuralNetOperator>(preNode)) {
      LOG(WARNING) << "Can not fuse Conv Sum";
      continue;
    }

    nn->dataFlow.createEdge(sumInputX, convNode);
    auto newOutputName = repr::nn::get<repr::Tensor>(sumInputX)->getName()
      + "_fusion_fix_" + std::to_string(i);

    auto newInputTensor = util::make_unique<repr::Tensor>(newOutputName);
    auto newInput = nn->dataFlow.createNode(
        unique_dyn_cast<repr::NeuralNetData>(newInputTensor));

    nn->dataFlow.replaceNode(sumInputX, newInput);
    nn->dataFlow.deleteNode(sumInputX);

    auto newOutputTensor = util::make_unique<repr::Tensor>(newOutputName);
    auto newOutput = nn->dataFlow.createNode(
        unique_dyn_cast<repr::NeuralNetData>(newOutputTensor));

    auto sumOutput = repr::nn::getOutputs(sumNode).front();
    nn->dataFlow.replaceNode(sumOutput, newOutput);
    nn->dataFlow.createEdge(convNode, newOutput);

    if (!is_dnnlowp_sum) {
      resetConvForFusion(convNode, FUSION_CONV_SUM);
    } else {
      moveOpArg(ws, "Y_scale", sum, conv);
      moveOpArg(ws, "Y_zero_point", sum, conv);

      if (isOpType(sumNode, "Int8Sum")) {
        CAFFE_THROW("Unimplemented conv sum fusion");
      } else if (isOpType(sumNode, "Int8SumRelu")
          || isOpType(sumNode, "Int8AddRelu")) {
        convOp->set_type("Int8ConvSumRelu");
      } else {
        CAFFE_THROW("Unsupport operator in conv fusion");
      }
    }

    nn->dataFlow.deleteNode(sumNode);
    nn->dataFlow.deleteNode(sumOutput);
    nn->dataFlow.deleteNode(convOutput);
  }
}

void fuseActivationForIdeep(repr::NNModule* nn) {
  // Conv+Relu fusion
  auto should_fuse = shouldFuseConv;
  auto postprocess = std::bind(resetConvForFusion, std::placeholders::_1, 1);
  fuseActivation<repr::Conv, repr::Relu>(nn, should_fuse, postprocess);
}

bool enforceInplaceInSumFusion(repr::NNModule *nn, caffe2::Workspace *ws) {
  // For fusions of Conv+Sum or Conv+Sum+ReLU, the last input and output must
  // be inplaced. To enforce inplace, here to re-check whole graph and correct
  // the ConvFusion Ops.
  bool ret = false;
  auto allNodes = nn->dataFlow.getMutableNodes();
  for (int i = allNodes.size() - 1; i > 0; i--) {
    auto convNode = allNodes[i];
    if (convNode == nullptr
      || !repr::nn::is<repr::NeuralNetOperator>(convNode)) {
      continue;
    }

    auto conv = repr::nn::get<repr::NeuralNetOperator>(convNode);
    if (!isOnIdeepDevice(*conv)) {
      LOG(WARNING) << "Not a IDEEP operator";
      continue;
    }

    if (repr::nn::is<repr::Conv>(convNode)) {
      if (!isConvFusion(convNode, FUSION_CONV_SUM)
          && !isConvFusion(convNode, FUSION_CONV_SUM_RELU))
        continue;
    } else if (!isOpType(convNode, "Int8ConvSum")
        && !isOpType(convNode, "Int8ConvSumRelu")) {
        continue;
    }

    auto convInput = repr::nn::getInputs(convNode).back();
    auto inputName = repr::nn::get<repr::Tensor>(convInput)->getName();
    auto convOutput = repr::nn::getOutputs(convNode).front();
    auto outputName = repr::nn::get<repr::Tensor>(convOutput)->getName();
    if (inputName == outputName) {
      continue;
    }

    auto consumer = repr::nn::getConsumers(convInput).back();
    if (consumer != convNode) {
      LOG(ERROR) << "Can not enforce to inplace for fusion";
      return false;
    }

    auto newOutputTensor = util::make_unique<repr::Tensor>(inputName);
    auto newOutput = nn->dataFlow.createNode(
        unique_dyn_cast<repr::NeuralNetData>(newOutputTensor));
    nn->dataFlow.replaceNode(convOutput, newOutput);
    nn->dataFlow.deleteNode(convOutput);

    ret = true;
  }
  return ret;
}

void enforceFusionInplaceForIdeep(repr::NNModule *nn, caffe2::Workspace *ws) {
  while (enforceInplaceInSumFusion(nn, ws)) {}
}

void setPoolingInferenceMode(repr::NNModule *nn) {
  for (auto node_pair : repr::nn::dataIterator<repr::MaxPool>(nn->dataFlow)) {
    repr::NNGraph::NodeRef maxPoolNode;
    repr::MaxPool *maxPool;
    std::tie(maxPool, maxPoolNode) = node_pair;

    if (!isOnIdeepDevice(*maxPool)) {
      LOG(WARNING) << "Not a IDEEP operator";
      continue;
    }

    auto *op = getMutableOpDef(*maxPool);
    bool found_training_mode = false;
    for (auto &arg : *op->mutable_arg()) {
      if (arg.name() == "training_mode") {
        arg.set_i(0);
        found_training_mode = true;
        break;
      }
    }

    if (!found_training_mode) {
      auto *arg = op->add_arg();
      arg->set_name("training_mode");
      arg->set_i(0);
    }
  }
}

void fuseOrderSwitchAtTop(repr::NNModule *nn) {
  auto allNodes = nn->dataFlow.getMutableNodes();
  if (allNodes.size() < 2)
    return;

  int i = 0;
  for (; i < allNodes.size(); i++) {
    if (repr::nn::is<repr::NeuralNetOperator>(allNodes[i]))
      break;
  }

  auto osNode = allNodes[i];
  if (osNode == nullptr || !isOpType(osNode, "NCHW2NHWC")) {
    return;
  }

  auto osOutput = repr::nn::getOutputs(osNode).front();
  auto osConsumers = repr::nn::getConsumers(osOutput);
  if (osConsumers.size() != 1) {
    return;
  }

  auto quantizeNode = osConsumers.back();
  if (quantizeNode == nullptr || !isOpType(quantizeNode, "Int8Quantize")) {
    return;
  }

  auto osInput = repr::nn::getInputs(osNode).front();
  auto quantizeInput = repr::nn::getInputs(quantizeNode).front();
  nn->dataFlow.replaceNode(quantizeInput, osInput);

  auto quantize = repr::nn::get<repr::NeuralNetOperator>(quantizeNode);
  auto *quantizeOp = getMutableOpDef(*quantize);
  auto *arg = quantizeOp->add_arg();
  arg->set_name("output_order");
  arg->set_i(iformat::nhwc);

  nn->dataFlow.deleteNode(osNode);
  nn->dataFlow.deleteNode(osOutput);
  nn->dataFlow.deleteNode(quantizeInput);
}

void fuseDequantize(repr::NNModule *nn) {
  auto allNodes = nn->dataFlow.getMutableNodes();
  for (int i = allNodes.size() - 1; i > 0; i--) {
    auto dequantizeNode = allNodes[i];
    if (dequantizeNode == nullptr
      || !repr::nn::is<repr::NeuralNetOperator>(dequantizeNode)) {
      continue;
    }

    auto dequantize = repr::nn::get<repr::NeuralNetOperator>(dequantizeNode);
    if (!isOnIdeepDevice(*dequantize)) {
      LOG(WARNING) << "Not a IDEEP operator";
      continue;
    }

    if (!isOpType(dequantizeNode, "Int8Dequantize")) {
      continue;
    }

    auto dequantizeInput = repr::nn::getInputs(dequantizeNode).back();
    if (dequantizeInput->getInEdges().size() <= 0) {
      continue;
    }

    auto consumers = repr::nn::getConsumers(dequantizeInput);
    if (consumers.size() != 1) {
      continue;
    }

    auto preNode = repr::nn::getProducer(dequantizeInput);
    auto pre = repr::nn::get<repr::NeuralNetOperator>(preNode);
    removeArg(*pre, "Y_scale");
    removeArg(*pre, "Y_zero_point");

    auto dequantizeOutput = repr::nn::getOutputs(dequantizeNode).front();
    nn->dataFlow.replaceNode(dequantizeOutput, dequantizeInput);

    nn->dataFlow.deleteNode(dequantizeNode);
    nn->dataFlow.deleteNode(dequantizeOutput);
  }
}

void OptimizeForIdeep(
    repr::NNModule* nn,
    caffe2::Workspace* ws,
    bool training_mode) {
  if (training_mode) {
    // Only support inference so far
    return;
  }

  removeStopGradientForInference(nn);

  fuseConvBNAndAffChForIdeep(nn, ws);

  fuseConvSumForIdeep(nn, ws);

  fuseActivationForIdeep(nn);

  enforceFusionInplaceForIdeep(nn, ws);

  setPoolingInferenceMode(nn);

  fuseOrderSwitchAtTop(nn);

  fuseDequantize(nn);
}

#endif // CAFFE2_USE_MKLDNN

} // namespace opt
} // namespace caffe2
