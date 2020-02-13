#pragma once

#include <torch/csrc/WindowsTorchApiMacro.h>

#include <vector>

namespace torch {
namespace jit {
namespace fuser {

struct Statement;
struct Val;
struct Expr;

struct TensorDomain;
struct TensorView;
struct IterDomain;
struct Tensor;
struct Float;
struct Int;

struct UnaryOp;
struct BinaryOp;
struct Split;
struct Merge;
struct Reorder;

struct FusionGurad;
struct Fusion;


struct TORCH_API IterVisitor {
  virtual ~IterVisitor() = default;

public:
  IterVisitor() = default;

  IterVisitor(const IterVisitor& other) = default;
  IterVisitor& operator=(const IterVisitor& other) = default;

  IterVisitor(IterVisitor&& other) = default;
  IterVisitor& operator=(IterVisitor&& other) = default;

  std::vector<const Statement*> next(const Statement* const stmt);
  std::vector<const Statement*> next(const Expr* const expr);
  std::vector<const Statement*> next(const Val* const v);

  virtual void handle(const Statement* const stmt);
  
  virtual void handle(const Expr* const);
  virtual void handle(const Val* const);
  
  virtual void handle(const TensorDomain* const);
  virtual void handle(const TensorView* const);
  virtual void handle(const IterDomain* const);
  virtual void handle(const Tensor* const);

  virtual void handle(const Float* const);
  virtual void handle(const Int* const);
  
  virtual void handle(const UnaryOp* const);
  virtual void handle(const BinaryOp* const);
  virtual void handle(const Split* const);
  virtual void handle(const Merge* const);
  virtual void handle(const Reorder* const);

  void traverse(const Fusion* const _fusion,  bool from_outputs_only, bool breadth_first);

};

}}} //torch::jit::fuser
