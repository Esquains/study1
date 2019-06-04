#include <torch/csrc/jit/passes/alias_analysis.h>
#include <torch/csrc/jit/passes/bailout_graph.h>
#include <torch/csrc/jit/passes/constant_pooling.h>
#include <memory>
#include <unordered_set>

namespace torch {
namespace jit {

struct BailOutGraphBuilderForNode {
  BailOutGraphBuilderForNode(std::shared_ptr<Graph> graph)
      : graph_(std::move(graph)) {}

  Value* addNewInputForValue(Value* old_value, Value* new_value = nullptr) {
    auto node = old_value->node();
    // this reduces the number of inputs to a bailout graph significantly
    // making it easier to debug
    if (node->kind() == prim::Constant) {
      auto new_const = copy_graph_->createClone(node, {nullptr});
      copy_graph_->block()->appendNode(new_const);
      return new_const->output();
    }

    if (!new_value) {
      new_value = copy_graph_->block()->addInput();
      live_inputs_.push_back(old_value);
    }
    this->old_to_new_[old_value] = new_value;
    new_value->copyMetadata(old_value);
    return new_value;
  }

  void buildBailOutBlockFrom(Node* n) {
    auto outer_node = n->owningBlock()->owningNode();
    auto* block = copy_graph_->block();
    if (n->kind() == prim::Loop) {
      auto new_max_count = addNewInputForValue(n->inputs()[0]);
      auto cur_iter = addNewInputForValue(n->blocks()[0]->inputs()[0]);
      // subtract the number of iterations we already did
      auto updated_max_trip_count = copy_graph_->create(aten::sub);
      block->appendNode(updated_max_trip_count);
      updated_max_trip_count->addInput(new_max_count);
      updated_max_trip_count->addInput(cur_iter);
      addNewInputForValue(n->inputs()[0], updated_max_trip_count->output());
      // N.B. the rest of inputs have already been mapped
      // when loop->blocks()[0] was processed
    } else if (n->kind() == prim::If) {
      // N.B. nothing to do; outputs should've already been mapped properly
      // when we processed if block (either true of false arm)
      // skip if since the rest of true or false block has already been
      // inlined
      n = n->next();
    }

    auto b = n->owningBlock();
    // start from a given node, it could be any node anywhere in a graph
    // if it happens to be an if node, we advance past it (see the above)
    graph_node_list_iterator it(n, kNextDirection);
    for (; it != b->nodes().end(); it++) {
      auto env = [this](Value* v) {
        auto new_value =
            (this->old_to_new_.count(v) == 0) ? nullptr : this->old_to_new_[v];
        return addNewInputForValue(v, new_value);
      };
      auto node = *it;

      auto new_node = block->appendNode(copy_graph_->createClone(node, env));
      for (size_t i = 0; i < node->outputs().size(); ++i) {
        auto oo = node->outputs()[i];
        auto no = new_node->outputs()[i];
        old_to_new_[oo] = no;
        no->copyMetadata(oo);
      }
    }

    if (outer_node) {
      auto block_outputs = n->owningBlock()->outputs();
      // skip the first input for loops (current iteration count)
      size_t i = outer_node->kind() == prim::Loop;
      auto new_outputs = outer_node->kind() == prim::Loop
          ? outer_node->inputs()
          : outer_node->outputs();
      for (; i < block_outputs.size(); i++) {
        auto nv = old_to_new_[block_outputs[i]];
        old_to_new_[new_outputs.at(i)] = nv;
      }
      buildBailOutBlockFrom(outer_node);
    }
  }

  std::shared_ptr<Graph> buildBailOutGraphFrom(Node* n) {
    old_to_new_.clear();
    copy_graph_ = std::make_shared<Graph>();
    buildBailOutBlockFrom(n);
    // add graph outputs
    for (auto ov : graph_->outputs()) {
      auto new_value =
          (this->old_to_new_.count(ov) == 0) ? nullptr : this->old_to_new_[ov];
      auto nv = addNewInputForValue(ov, new_value);
      copy_graph_->registerOutput(nv);
    }
    return copy_graph_;
  }

  std::shared_ptr<Graph> graph_;
  std::shared_ptr<Graph> copy_graph_;
  std::vector<Value*> live_inputs_;
  std::unordered_map<Value*, Value*> old_to_new_;
};

struct BailOutInserter {
  BailOutInserter(std::shared_ptr<Graph> graph) : graph_(std::move(graph)) {}

  void run() {
    insertBailOuts(graph_->block());
    // set bailout subgraphs
    setSubgraphs();
  }

  // remove all BailOuts and Guards
  // we will profile this graph
  // and insert guards and bailouts again
  // where needed if its guard fails
  void sanitizeGraph(Block* b) {
    for (auto it = b->nodes().begin(); it != b->nodes().end(); ++it) {
      if (it->kind() == prim::Guard) {
        // this will need to be profiled again
        it->input()->setType(TensorType::create());
        // destroy the guard
        it->output()->replaceAllUsesWith(it->input());
        it.destroyCurrent();
      }

      if (it->kind() == prim::BailOut) {
        // this is the index of an input we are actually guarding
        auto index = it->i(attr::slot);
        // this will need to be profiled again
        it->inputs()[index]->setType(TensorType::create());
        // destroy the guard
        it->output()->replaceAllUsesWith(it->inputs()[index]);
        it.destroyCurrent();
      }

      for (auto ib : it->blocks()) {
        sanitizeGraph(ib);
      }
    }
  }

  void setSubgraphs() {
    for (auto e : subgraphs) {
      sanitizeGraph(e.second->block());
      ConstantPooling(e.second);
      e.first->g_(attr::Subgraph, e.second);
    }
  }

  void insertBailOuts(Block* b) {
    for (auto it = b->nodes().begin(); it != b->nodes().end(); ++it) {
      if (it->kind() == prim::Guard) {
        auto bailout_node = b->owningGraph()->create(prim::BailOut);
        auto node = *it;

        BailOutGraphBuilderForNode bg(graph_);
        auto bailout_graph = bg.buildBailOutGraphFrom(node);

        // N.B. we don't immediately set SubGraph attribute
        // since we might need to copy this node the second time
        // if the node is in a loop which then gets copied again and again
        subgraphs.insert({bailout_node, bailout_graph});

        for (size_t i = 0; i < bg.live_inputs_.size(); i++) {
          bailout_node->addInput(bg.live_inputs_[i]);

          // to tell which input (index offset)
          // we are actually supposed to guard
          if (it->input() == bg.live_inputs_[i]) {
            bailout_node->i_(attr::slot, i);
            bailout_node->output()->setType(it->output()->type());
          }
        }
        bailout_node->insertBefore(*it);
        it->output()->replaceAllUsesWith(bailout_node->output());
        it.destroyCurrent();
      } else {
        for (auto ib : it->blocks()) {
          insertBailOuts(ib);
        }
      }
    }
  }

  std::shared_ptr<Graph> graph_;
  std::unordered_map<Node*, std::shared_ptr<Graph>> subgraphs;
};

void InsertBailOuts(std::shared_ptr<Graph> graph) {
  BailOutInserter ibo(std::move(graph));
  ibo.run();
}

} // namespace jit
} // namespace torch
