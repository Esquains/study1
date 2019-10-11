#pragma once

#include <torch/arg.h>
#include <torch/csrc/WindowsTorchApiMacro.h>
#include <torch/types.h>

namespace torch {
namespace nn {

/// Options for a L1 loss module.
struct TORCH_API L1LossOptions {
  L1LossOptions(Reduction::Reduction reduction = Reduction::Mean)
      : reduction_(reduction) {}

  /// Specifies the reduction to apply to the output.
  TORCH_ARG(Reduction::Reduction, reduction);
};

// ============================================================================

/// Options for a Hinge Embedding loss functional and module.
struct TORCH_API HingeEmbeddingLossOptions {
  /// Specifies the threshold for which the distance of a negative sample must
  /// reach in order to incur zero loss. Default: 1
  TORCH_ARG(double, margin) = 1.0;
  /// Specifies the reduction to apply to the output. Default: Mean
  TORCH_ARG(Reduction::Reduction, reduction) = Reduction::Mean;
};

// ============================================================================

/// Options for a Hinge Embedding loss functional and module.
struct TORCH_API CosineEmbeddingLossOptions {
  /// Specifies the threshold for which the distance of a negative sample must
  /// reach in order to incur zero loss. Should be a number from -1 to 1, 0
  /// to 0.5 is suggested. Default: 0.0
  TORCH_ARG(double, margin) = 0.0;
  /// Specifies the reduction to apply to the output. Default: Mean
  TORCH_ARG(Reduction::Reduction, reduction) = Reduction::Mean;
};

// ============================================================================

/// Options for a smooth L1 loss functional and module.
struct TORCH_API SmoothL1LossOptions {
  SmoothL1LossOptions(Reduction::Reduction reduction = Reduction::Mean)
    : reduction_(reduction) {}

  /// Specifies the reduction to apply to the output: 'none' | 'mean' | 'sum'.
  /// 'none': no reduction will be applied, 'mean': the sum of the output will
  /// be divided by the number of elements in the output, 'sum': the output will
  /// be summed. Default: 'mean'
  TORCH_ARG(Reduction::Reduction, reduction);
};

} // namespace nn
} // namespace torch
