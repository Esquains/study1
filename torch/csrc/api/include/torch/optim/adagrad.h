#pragma once

#include <torch/nn/pimpl.h>
#include <torch/optim/optimizer.h>
#include <torch/optim/serialize.h>
#include <torch/types.h>

#include <utility>
#include <vector>

namespace torch {
namespace serialize {
class OutputArchive;
class InputArchive;
} // namespace serialize
} // namespace torch

namespace torch {
namespace optim {

struct TORCH_API AdagradOptions {
  AdagradOptions(double learning_rate);
  TORCH_ARG(double, learning_rate);
  TORCH_ARG(double, lr_decay) = 0;
  TORCH_ARG(double, weight_decay) = 0;
  TORCH_ARG(double, initial_accumulator_value) = 0;
  TORCH_ARG(double, eps) = 1e-10;
};

class TORCH_API Adagrad : public Optimizer {
 public:
  template <typename ParameterContainer>
  explicit Adagrad(
      ParameterContainer&& parameters,
      const AdagradOptions& options_) : Optimizer(std::forward<ParameterContainer>(parameters)), options(options_) {
    TORCH_CHECK(options.learning_rate() >= 0, "Invalid learning rate: ", options.learning_rate());
    TORCH_CHECK(options.lr_decay() >= 0, "Invalid lr_decay value: ", options.lr_decay());
    TORCH_CHECK(options.weight_decay() >= 0, "Invalid weight_decay value: ", options.weight_decay());
    TORCH_CHECK(options.initial_accumulator_value() >= 0, "Invalid initial_accumulator_value value: ", options.initial_accumulator_value());
    TORCH_CHECK(options.eps() >= 0, "Invalid epsilon value: ", options.eps());
  }

  //cross check
  template <typename ParameterContainer>
  explicit Adagrad(
      ParameterContainer parameters,
      const AdagradOptions& options_) : Optimizer(std::forward<ParameterContainer>(parameters), options_), options(options_) {

  }
  void step() override;

  AdagradOptions options;

  void save(serialize::OutputArchive& archive) const override;
  void load(serialize::InputArchive& archive) override;

  std::vector<Tensor> sum_buffers;
  std::vector<int64_t> step_buffers;

 private:
  Adagrad() : options(0) {}

  template <typename Self, typename Archive>
  static void serialize(Self& self, Archive& archive) {
    _TORCH_OPTIM_SERIALIZE(sum_buffers);
    _TORCH_OPTIM_SERIALIZE(step_buffers);
  }
};
} // namespace optim
} // namespace torch
