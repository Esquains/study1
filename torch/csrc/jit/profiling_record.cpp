#include <torch/csrc/jit/profiling_record.h>
#include <torch/csrc/jit/graph_executor.h>
#include <torch/csrc/jit/interpreter.h>
#include <torch/csrc/jit/passes/constant_propagation.h>
#include <torch/csrc/jit/jit_log.h>
#include <ostream>

namespace c10 {

std::ostream& operator<<(std::ostream& os, const c10::ShapeSymbol& s)
{
    os << "ShapeSymbol(" << s.value_ << ',' << s.statik_ << ')';
    return os;
}

}

namespace torch {
namespace jit {

c10::ShapeSymbol ShapeSymbolTable::toSymbol(
    c10::ShapeSymbol val,
    std::map<c10::ShapeSymbol, c10::ShapeSymbol>& dims2symbols,
    ProfilingRecord* pr) {
  if (dims2symbols.count(val) == 0) {
    auto new_sym = pr->getNewSymbol();
    dims2symbols[val] = new_sym;
    return new_sym;
  }

  return dims2symbols[val];
}

c10::ShapeSymbol ShapeSymbolTable::GetSymbolInSet(c10::ShapeSymbol new_size, c10::ShapeSymbol set, ProfilingRecord* pr) {
  auto& dims2symbols = sets_.getSetForSymbol(set);
  auto new_sym = toSymbol(new_size, dims2symbols, pr);
  return new_sym;
}

ProfilingRecord::ProfilingRecord(std::shared_ptr<Graph> g)
    : profiled_graph_(std::move(g)), profiling_count_(getNumProfiledRuns()) {}

ProfileOp* ProfilingRecord::createProfileNode(
    const std::function<void(Stack&)>& fp,
    at::ArrayRef<Value*> inputs) {
  auto pn = new ProfileOp(profiled_graph_.get(), fp);

  for (auto in : inputs) {
    pn->addInput(in);
  }
  return pn;
}

static void unprofileGraphInputs(const std::shared_ptr<Graph> &graph) {
  for (auto i : graph->inputs()) {
    if (i->type()->isSubtypeOf(TensorType::get())) {
      i->setType(unshapedType(i->type()));
    }
  }
}

static void unprofileBlock(Block* start_block) {
  std::vector<Block*> stack;
  stack.push_back(start_block);

  while (!stack.empty()) {
    Block* block = stack.back();
    stack.pop_back();

    for (auto n : block->nodes()) {
      for (auto o : n->outputs()) {
        if (o->type()->isSubtypeOf(TensorType::get())) {
          o->setType(unshapedType(o->type()));
        }
      }
      stack.insert(stack.end(), n->blocks().begin(), n->blocks().end());
    }
  }
}



// struct SymbolShape {
//   SymbolShape(int64_t v, bool statik = false): value_(v), statik_(statik) {};
//   int64_t value_;
//   bool statik_;
// };

//getNewSymbolInNamespace
//getNewSymbolInGlobalNamespace
//geGlobalNamespace
//getNamespaceForSymbol

std::vector<c10::ShapeSymbol> ProfilingRecord::mergeSymbolicShapes(
    const std::vector<c10::ShapeSymbol>& new_sizes,
    c10::optional<std::vector<c10::ShapeSymbol>> sym_shapes,
    ShapeSymbolTable& symbol_table) {
  std::vector<c10::ShapeSymbol> new_symbols;
  if (sym_shapes.has_value() && new_sizes.size() == (*sym_shapes).size()) {
    for (size_t i = 0; i < new_sizes.size(); i++) {
      auto symbol = (*sym_shapes)[i];
      GRAPH_DEBUG("Merging symbol ", symbol);
      if (!symbol_table.isBound(symbol)) {
        symbol_table.assign(symbol, new_sizes[i]);
        GRAPH_DEBUG(symbol, " is now bound to ", new_sizes[i]);
        new_symbols.push_back(symbol);
      } else {
        if (symbol_table.getValue(symbol) == new_sizes[i]) {
          GRAPH_DEBUG("Reusing symbol ", symbol);
          new_symbols.push_back(symbol);
        } else {
          auto new_sym = symbol_table.GetSymbolInSet(new_sizes[i], symbol, this);
          GRAPH_DEBUG(symbol, " is already bound to ", symbol_table.getValue(symbol), " assigning ", new_sizes[i], " a new symbol ", new_sym);
          new_symbols.push_back(new_sym);
        }
      }
    }
  }
  return new_symbols;
}

// if we create Symbols that are just dims they are unique in global namespace for just the first run
// but in the next runs they are only unique in SymbolNamespace
// split_symbols_ create a set relationship immediately without a need for an intermediate DimCell

// DimCell doesn't eliminate the need for DimCell -> Symbol mapping
// and doesn't simplify splitting because the new symbol is only valid in the namespace of the old set


// within one run
// static shapes assign symbols can be static shapes 
// bind symbols, via a symbol table
// resolve conflicts
// SymbolNamespace 

void ProfilingRecord::insertShapeProfile(Node *n, Value *i) {

  auto pn = createProfileNode(nullptr, {i});
  auto pno = pn->addOutput();
  bool first = true;
  pno->setType(TensorType::get());
  std::function<void(Stack&)> shape_profiler = [this, pno, first](
                                                   Stack& stack) mutable {
    int64_t frame_id;
    pop(stack, frame_id);
    IValue v;
    pop(stack, v);
    if (v.isTensor()) {
      std::lock_guard<std::mutex> lock(this->mutex_);
      auto& profiled_types = profiled_types_per_frame_[frame_id];
      auto t = v.toTensor();
      if (t.defined()) {
        auto pttp = tensorTypeInCurrentExecutionContext(t);
        GRAPH_DEBUG(
          "In run ",
          frame_id,
          " annotating %",
          pno->debugName(),
          " with ",
          *pttp);
        if (first) {
          first = false;
          profiled_types.insert({pno, pttp});
        } else {
          auto type = profiled_types.at(pno);
          GRAPH_DEBUG("Existing type for %", pno->debugName(), " ", *type);
          auto& symbol_table = symbols_per_frame_[frame_id];
          auto sizes = fmap(t.sizes(), [](int64_t s){return c10::ShapeSymbol(s, true); });
          auto new_sym_shapes = this->mergeSymbolicShapes(
              sizes,
              type->symbolic_sizes(),
              symbol_table);
          pttp = type->merge(pttp)->withSymbolicShapes(new_sym_shapes);
          GRAPH_DEBUG(
              "Result for %",
              pno->debugName(),
              " ",
              *pttp);
          profiled_types[pno] = pttp;
        }
      } else {
        profiled_types[pno] = TensorType::get()->withUndefined();
      }
    }
    // passing t through
    push(stack, v);
  };

  pn->setCallback(shape_profiler);
  pn->insertBefore(n);
  n->replaceInputWith(i, pn->output());
}

void ProfilingRecord::instrumentBlock(Block *block) {
  for (auto it = block->nodes().begin(); it != block->nodes().end(); ++it) {
    auto n = *it;
    for (auto i : n->inputs()) {
      if (!i->type()->isSubtypeOf(TensorType::get()) ||
          i->node()->kind() == prim::profile) {
        continue;
      }

      insertShapeProfile(n, i);
    }

    for (auto b : n->blocks()) {
      instrumentBlock(b);
    }
  }
}

std::unique_ptr<ProfilingRecord> ProfilingRecord::instrumentGraph(
    const std::shared_ptr<Graph>& graph) {
  auto new_g = graph->copy();
  auto pr = std::unique_ptr<ProfilingRecord>(new ProfilingRecord(new_g));
  auto raw_pr = pr.get();
  unprofileGraphInputs(new_g);
  unprofileBlock(new_g->block());
  pr->instrumentBlock(new_g->block());

  for (auto i : new_g->return_node()->inputs()) {
    if (i->type()->isSubtypeOf(TensorType::get())) {
      pr->insertShapeProfile(new_g->return_node(), i);
    }
  }
  std::function<void(Stack&)> counter = [raw_pr](Stack& stack) {
    int64_t frame_id;
    pop(stack, frame_id);

    std::lock_guard<std::mutex> lock(raw_pr->mutex_);

    if (raw_pr->profiling_count_ > 0) {
      raw_pr->profiling_count_--;
    }

    // merge profiling information from all runs
    if (raw_pr->profiling_count_ == 0) {
      GRAPH_DEBUG("Collected ", raw_pr->profiled_types_per_frame_.size(), " records for run ", frame_id);

      auto profiled_types_iter = raw_pr->profiled_types_per_frame_.begin();
      // scratch symbol table and symbol sets for merging profiling information
      // from multiple runs
      auto frame_id = profiled_types_iter->first;
      auto merged_profiled_types = profiled_types_iter->second;
      auto merged_symbol_table = raw_pr->symbols_per_frame_[frame_id];
      profiled_types_iter++;

      // merge profiling information from next runs into the first one
      for (; profiled_types_iter != raw_pr->profiled_types_per_frame_.end(); profiled_types_iter++) {
        merged_symbol_table.reset();
        for (auto val_type_pair : profiled_types_iter->second) {
          if (merged_profiled_types.count(val_type_pair.first) == 0) {
            merged_profiled_types[val_type_pair.first] = val_type_pair.second;
            auto type = merged_profiled_types[val_type_pair.first];
            if (type->symbolic_sizes().has_value()) {
              std::vector<c10::ShapeSymbol> new_symbols;
              for (size_t i = 0; i < (*type->symbolic_sizes()).size(); i++) {
                auto old_symbol = (*type->symbolic_sizes())[i];
                if (old_symbol.statik_) {
                  new_symbols.push_back(old_symbol);
                }
                else {
                  new_symbols.push_back(
                    merged_symbol_table.GetSymbolInSet(old_symbol, c10::ShapeSymbol::getInvalidSymbol(), raw_pr));
                }
              }
              auto new_type =
                  type->withSymbolicShapes(new_symbols);
              GRAPH_DEBUG(
                  "(Absent merged type) Merging ",
                  *type,
                  " of run ",
                  profiled_types_iter->first,
                  " into ",
                  *new_type);
              merged_profiled_types[val_type_pair.first] = new_type;
            }
          } else {
            const auto& concrete_sizes = val_type_pair.second->symbolic_sizes();
            if (concrete_sizes.has_value()) {
              auto type = merged_profiled_types[val_type_pair.first];
              auto new_shape = raw_pr->mergeSymbolicShapes(
                  *concrete_sizes,
                  type->symbolic_sizes(),
                  merged_symbol_table);
              GRAPH_DEBUG(
                  "Merging ",
                  *val_type_pair.second,
                  " of run ",
                  profiled_types_iter->first,
                  " into ",
                  *type);
              type = type->merge(val_type_pair.second);
              type = type->withSymbolicShapes(new_shape);
              GRAPH_DEBUG("Result : ", *type);
              merged_profiled_types[val_type_pair.first] = type;
            } else {
              // reset symbolic shapes when ranks are different
              auto type = merged_profiled_types[val_type_pair.first];
              type = type->merge(val_type_pair.second);
              type = type->withSymbolicShapes(c10::nullopt);
              merged_profiled_types[val_type_pair.first] = type;
            }
          }
        }
      }

      // update types in the graph
      for (auto val_type_pair : merged_profiled_types) {
        val_type_pair.first->setType(val_type_pair.second);
      }
    }
  };

  auto pop = pr->createProfileNode(counter, {});
  new_g->appendNode(pop);
  return pr;
}

} // namespace jit
} // namespace torch
