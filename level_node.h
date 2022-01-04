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
  std::shared_ptr<Statistics> stats_, child_stats_;
  
  LevelNode() : next_(nullptr), buffer_() {}
  LevelNode(std::shared_ptr<SBSNode> next) 
  : next_(next), buffer_() {}
  void Add(std::shared_ptr<BoundedValue> value) { buffer_.Add(value); }
  void Del(std::shared_ptr<BoundedValue> value) { buffer_.Del(*value); }
  bool Contains(std::shared_ptr<BoundedValue> value) const { return buffer_.Contains(*value); }
  bool Overlap() const { return buffer_.Overlap(); }
  void Absorb(const LevelNode& node) {
    next_ = node.next_;
    buffer_.AddAll(node.buffer_);
    paras_->Absorb(*node.paras_);
  }
  void Shrink(double k) { paras_->Shrink(k); }
  bool isDirty() const { return !buffer_.empty(); }
  void BuildCopyStatistics(const Statistics& stats, double k) { 
    if (k == 0)
      stats_ = std::make_shared<Statistics>(stats.options_);
    else 
      stats_ = std::make_shared<Statistics>(stats);
    if (k < 1)
      stats_->Shrink(k);
    assert(k <= 1);
  }
};

}