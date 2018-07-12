#pragma once

#include "torch/csrc/autograd/function.h"
#include "torch/csrc/autograd/variable.h"
#include "torch/csrc/autograd/symbolic.h"

#include <memory>
#include <string>
#include <vector>

namespace torch { namespace autograd {

struct Error : public Function {
  Error(std::string msg, edge_list&& next_edges)
    : Function(std::move(next_edges))
    , msg(std::move(msg)) {}

  Error(std::string msg)
    : msg(std::move(msg)) {}

  virtual variable_list apply(const variable_list& inputs) override;

  std::string msg;
};

// Identity in forward, Error in backward. Used to implement @once_differentiable
struct DelayedError : public Function {
  DelayedError(std::string msg, int num_inputs)
    : msg(std::move(msg)) {
      for (int i = 0; i < num_inputs; i++)
        input_metadata_.emplace_back();
    }

  virtual variable_list apply(const variable_list& inputs) override;

  std::string msg;

  uint32_t input_nr;
};

struct GraphRoot : public Function {
  GraphRoot(edge_list functions, variable_list inputs)
      : Function(std::move(functions)),
        outputs(std::move(inputs)) {}

  virtual variable_list apply(const variable_list& inputs) {
    return outputs;
  }

  variable_list outputs;
};

}}
