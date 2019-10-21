#pragma once

#include <torch/nn/options/vision.h>
#include <torch/types.h>

namespace torch {
namespace nn {
namespace functional {

inline Tensor affine_grid(
    const Tensor& theta,
    const IntArrayRef& size,
    bool align_corners = false) {
  // enforce floating point dtype on theta
  TORCH_CHECK(
      theta.is_floating_point(),
      "Expected theta to have floating point type, but got ",
      theta.dtype());

  // check that shapes and sizes match
  if (size.size() == 4) {
    TORCH_CHECK(
        theta.dim() == 3 && theta.size(-2) == 2 && theta.size(-1) == 3,
        "Expected a batch of 2D affine matrices of shape Nx2x3 for size ",
        size,
        ". Got ",
        theta.sizes(), ".");
  } else if (size.size() == 5) {
    TORCH_CHECK(
        theta.dim() == 3 && theta.size(-2) == 3 && theta.size(-1) == 4,
        "Expected a batch of 3D affine matrices of shape Nx3x4 for size ",
        size,
        ". Got ",
        theta.sizes(), ".");
  } else {
    TORCH_CHECK(
        false,
        "affine_grid only supports 4D and 5D sizes, ",
        "for 2D and 3D affine transforms, respectively. ",
        "Got size ", size);
  }

  if (*std::min_element(size.begin(), size.end()) <= 0) {
    TORCH_CHECK(false, "Expected non-zero, positive output size. Got ", size);
  }

  return torch::affine_grid_generator(theta, size, align_corners);
}

inline Tensor grid_sample(
    const Tensor& input,
    const Tensor& grid,
    GridSampleOptions options = {}) {
  
  if (!options.align_corners().has_value()) {
    TORCH_WARN("Default grid_sample and affine_grid behavior will be changed ",
                "to align_corners=False from 1.4.0. ",
                "See the documentation of grid_sample for details.");
    options.align_corners(true);
  }
  
  return torch::grid_sampler(input, grid, static_cast<int64_t>(options.mode()), static_cast<int64_t>(options.padding_mode()), options.align_corners().value());
}

} // namespace functional
} // namespace nn
} // namespace torch
