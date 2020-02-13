#pragma once

#include <c10/util/Exception.h>
#include <c10/core/ScalarType.h>
#include <torch/csrc/WindowsTorchApiMacro.h>

#include <cstdint>
#include <iostream>
#include <string>

namespace torch {
namespace jit {
namespace fuser {

//Order of strength
enum class TORCH_API ValType {
  TensorDomain,
  TensorView,
  IterDomain,
  Tensor,
  Scalar
};

enum class TORCH_API DataType {
  Float,
  Int,
  Null
};

enum class TORCH_API ExprType {
<<<<<<< HEAD
  Add
  // Sub,
  // Mul,
  // Div,
  // Mod,
  // Loop,
  // Swap,
  // Merge,
  // Split,
  // Index,
=======
    UnaryOp
  , BinaryOp
  , Split
  , Merge
  , Reorder
// , Loop
// , Swap
// , Index
};

enum class TORCH_API UnaryOpType {
    Neg
  , Cast
};

enum class TORCH_API BinaryOpType {
    Add
  , Sub
  , Mul
  , Div
  , Mod
>>>>>>> Create BinaryOp and UnaryOp Exprs.
};

enum class ParallelType {
  BIDz,
  BIDy,
  BIDx,
  TIDz,
  TIDy,
  TIDx,
  Vectorize,
  Unroll,
  Serial
};


ValType promote_type(const ValType& t1, const ValType& t2);
DataType promote_type(const DataType& t1, const DataType& t2);
bool is_cast_legal(const DataType& t1, const DataType& t2);

DataType aten_to_data_type(const at::ScalarType& scalar_type);

TORCH_API std::ostream& operator<<(std::ostream&, const ValType);
TORCH_API std::ostream& operator<<(std::ostream&, const DataType);
TORCH_API std::ostream& operator<<(std::ostream&, const ExprType);
TORCH_API std::ostream& operator<<(std::ostream&, const UnaryOpType);
TORCH_API std::ostream& operator<<(std::ostream&, const BinaryOpType);
TORCH_API std::ostream& operator<<(std::ostream&, const ParallelType);

} // namespace fuser
} // namespace jit
} // namespace torch
