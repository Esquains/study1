#pragma once

#include <torch/expanding_array.h>
#include <torch/nn/cloneable.h>
#include <torch/nn/functional/loss.h>
#include <torch/nn/options/loss.h>
#include <torch/nn/pimpl.h>
#include <torch/types.h>

#include <torch/csrc/WindowsTorchApiMacro.h>

#include <cstddef>
#include <vector>

namespace torch {
namespace nn {

/// Creates a criterion that measures the mean absolute error (MAE) between each
/// element in the input : math :`x` and target : `y`.
struct TORCH_API L1LossImpl : Module {
  explicit L1LossImpl(const L1LossOptions& options_ = {});

  /// Pretty prints the `L1Loss` module into the given `stream`.
  void pretty_print(std::ostream& stream) const override;

  Tensor forward(const Tensor& input, const Tensor& target);

  /// The options with which this `Module` was constructed.
  L1LossOptions options;
};

/// A `ModuleHolder` subclass for `L1LossImpl`.
/// See the documentation for `L1LossImpl` class to learn what methods it
/// provides, or the documentation for `ModuleHolder` to learn about PyTorch's
/// module storage semantics.
TORCH_MODULE(L1Loss);

// ============================================================================

/// Creates a criterion that measures the loss given an input tensor :math:`x`
/// and a labels tensor :math:`y` (containing 1 or -1). This is usually used for
/// measuring whether two inputs are similar or dissimilar, e.g. using the L1
/// pairwise distance as :math:`x`, and is typically used for learning nonlinear
/// embeddings or semi-supervised learning.
struct TORCH_API HingeEmbeddingLossImpl : Module {
  explicit HingeEmbeddingLossImpl(
      const HingeEmbeddingLossOptions& options_ = {});

  /// Pretty prints the `HingeEmbeddingLoss` module into the given `stream`.
  void pretty_print(std::ostream& stream) const override;

  Tensor forward(const Tensor& input, const Tensor& target);

  /// The options with which this `Module` was constructed.
  HingeEmbeddingLossOptions options;
};

/// A `ModuleHolder` subclass for `HingeEmbeddingLossImpl`.
/// See the documentation for `HingeEmbeddingLossImpl` class to learn what
/// methods it provides, or the documentation for `ModuleHolder` to learn about
/// PyTorch's module storage semantics.
TORCH_MODULE(HingeEmbeddingLoss);

// ============================================================================

/// Creates a criterion that measures the triplet loss given an input
/// tensors :math:`x1`, :math:`x2`, :math:`x3` and a margin with a value greater 
/// than :math:`0`. This is used for measuring a relative similarity between
/// samples. A triplet is composed by `a`, `p` and `n` (i.e., `anchor`, 
/// `positive examples` and `negative examples` respectively). The
/// shapes of all input tensors should be :math:`(N, D)`
struct TORCH_API TripletMarginLossImpl : Module {
  explicit TripletMarginLossImpl(
      const TripletMarginLossOptions& options_ = {});

  /// Pretty prints the `TripletMarginLoss` module into the given `stream`.
  void pretty_print(std::ostream& stream) const override;

  Tensor forward(
      const Tensor& anchor,
      const Tensor& positive,
      const Tensor& negative);


  /// The options with which this `Module` was constructed.
  TripletMarginLossOptions options;
};

/// A `ModuleHolder` subclass for `TripletMarginLoss`.
/// See the documentation for `TripletMarginLossImpl` class to learn what
/// methods it provides, or the documentation for `ModuleHolder` to learn about
/// PyTorch's module storage semantics.
TORCH_MODULE(TripletMarginLoss);

} // namespace nn
} // namespace torch
