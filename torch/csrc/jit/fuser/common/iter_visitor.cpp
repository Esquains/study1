#include <torch/csrc/jit/fuser/common/fusion.h>
#include <torch/csrc/jit/fuser/common/ir.h>
#include <torch/csrc/jit/fuser/common/iter_visitor.h>
#include <torch/csrc/jit/fuser/common/type.h>
#include <deque>
#include <iostream>

namespace torch {
namespace jit {
namespace fuser {

std::vector<const Statement*> IterVisitor::next(
    const Statement* const statement) {
  if (statement->isVal())
    return next(static_cast<const Val*>(statement));
  else if (statement->isExpr())
    return next(static_cast<const Expr*>(statement));
  else
    throw std::runtime_error("Could not detect type in next_dispatch.");
}

std::vector<const Statement*> IterVisitor::next(const Val* const v) {
  if (FusionGuard::getCurFusion()->origin(v) != nullptr)
    return {FusionGuard::getCurFusion()->origin(v)};
  return {};
}

std::vector<const Statement*> IterVisitor::next(const Expr* const expr) {
  return {expr->inputs().begin(), expr->inputs().end()};
}

void IterVisitor::handle(const Statement* const stmt) {
  if (stmt->isVal())
    handle(static_cast<const Val*>(stmt));
  else if (stmt->isExpr())
    handle(static_cast<const Expr*>(stmt));
  else
    throw std::runtime_error("Unknown statment type in IterVisitor::handle(Statment).");
}

void IterVisitor::handle(const Expr* const expr) {
  expr->dispatch(this);
}

void IterVisitor::handle(const Val* const val) {
  val->dispatch(this);
}

void IterVisitor::handle(const Float* const f) {}

void IterVisitor::handle(const Tensor* const t) {}

void IterVisitor::handle(const Int* const i) {}

void IterVisitor::handle(const Add* const add) {}

void IterVisitor::traverse(const Fusion* const fusion, bool from_outputs_only, bool breadth_first) {
  if(breadth_first)
    throw std::runtime_error("Not implemented yet.");
  std::set<const Statement*> visited;
  std::deque<const Statement*> to_visit;

  if (FusionGuard::getCurFusion() != fusion)
    throw std::runtime_error("fusion is not active.");

  std::queue<const Val*> outputs_to_visit;

  if(from_outputs_only){
    for(const Val* out : fusion->outputs())
      outputs_to_visit.push(out);
  }else 
    for (const auto it : fusion->vals()) {
      if (fusion->uses().find(it) ==
          fusion->uses().end()) // never used, must be output or unused val
        outputs_to_visit.push(it);
    }

  while (!outputs_to_visit.empty()) {
    to_visit.push_front(outputs_to_visit.front());
    outputs_to_visit.pop();

    while (!to_visit.empty()) {
      const Statement* stmt = to_visit.front();

      for (const Statement* inp : next(stmt))
        if (visited.find(inp) == visited.end()) {
          to_visit.emplace_front(inp);
        }

      if (to_visit.front() != stmt) {
        continue;
      }

      to_visit.pop_front();
      if (visited.find(stmt) == visited.end()) {
        handle(stmt);
        visited.emplace(stmt);
      }
    }
  }

}

} // namespace fuser
} // namespace jit
} // namespace torch
