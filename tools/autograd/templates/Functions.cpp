// NB: Must be at the top of file to avoid including the deprecated "math.h".
// https://stackoverflow.com/questions/6563810/m-pi-works-with-math-h-but-not-with-cmath-in-visual-studio
#ifdef _MSC_VER
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#endif

#include "torch/csrc/autograd/generated/Functions.h"
#include <ATen/Utils.h>
#include <c10/core/TensorOptions.h>
#include <ATen/WrapDimUtils.h>
#include <ATen/WrapDimUtilsMulti.h>
#include <ATen/SparseTensorUtils.h>
#include <ATen/ExpandUtils.h>
#include <ATen/core/Reduction.h>
#include <ATen/Dispatch.h>
#include <ATen/ScalarOps.h>

#include <ciso646>
#include <algorithm>
#include <numeric>
#include <functional>

// ${generated_comment}

using at::Tensor;
using at::Scalar;
using at::IntArrayRef;
using at::TensorList;

namespace torch { namespace autograd { namespace generated {

namespace {

${manual_backward_functions}

bool any_variable_defined(variable_list& variables) {
  for (auto variable : variables) {
    if (variable.defined()) {
      return true;
    }
  }
  return false;
}

} // anonymous namespace

${autograd_function_definitions}

}}} // namespace torch::autograd::generated
