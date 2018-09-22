#pragma once

#include <ATen/core/DimVector.h>
#include <ATen/core/TensorImpl.h>
#include <ATen/core/context_base.h>
#include <ATen/core/context_base.h>

namespace caffe2 {
  using at::ToVectorTIndex;
  using at::size_from_dim_;
  using at::size_to_dim_;
  using at::size_between_dim_;
  using at::canonical_axis_index_;
  using at::TensorImpl;
}
