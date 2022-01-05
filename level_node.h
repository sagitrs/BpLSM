#pragma once

#include <vector>
#include <stack>
#include <memory>
#include "db/dbformat.h"
#include "bounded.h"
#include "bounded_value_container.h"
#include "options.h"
#include "statistics.h"
namespace sagitrs {
struct SBSNode;
typedef BoundedValueContainer TypeBuffer;

struct LevelNode {
  std::shared_ptr<SBSNode> next_;
  TypeBuffer buffer_;
  std::shared_ptr<Statistics> node_stats_;//, tree_stats_;
  
  // Build blank node.
  LevelNode(std::shared_ptr<StatisticsOptions> stat_options, std::shared_ptr<SBSNode> next = nullptr) 
  : next_(next), 
    buffer_(), 
    node_stats_(std::make_shared<Statistics>(stat_options)) {}
    
  void Add(std::shared_ptr<BoundedValue> value) { buffer_.Add(value); }
  void Del(std::shared_ptr<BoundedValue> value) { buffer_.Del(*value); }
  bool Contains(std::shared_ptr<BoundedValue> value) const { return buffer_.Contains(*value); }
  bool Overlap() const { return buffer_.Overlap(); }
  bool isDirty() const { return !buffer_.empty(); }

  void Absorb(const LevelNode& node) {
    next_ = node.next_;
    buffer_.AddAll(node.buffer_);
    node_stats_->Absorb(*node.node_stats_);
  }

};

}