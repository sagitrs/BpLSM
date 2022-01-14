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

  bool statistics_dirty_;
  std::shared_ptr<Statistics> tree_stats_;
  
  // Build blank node.
  LevelNode(std::shared_ptr<StatisticsOptions> stat_options, std::shared_ptr<SBSNode> next = nullptr) 
  : next_(next), 
    buffer_(), 
    tree_stats_(std::make_shared<Statistics>(stat_options)) {}

  void Add(std::shared_ptr<BoundedValue> value) { buffer_.Add( value); tree_stats_->MergeStatistics(*value); }
  void Del(std::shared_ptr<BoundedValue> value) { buffer_.Del(*value); statistics_dirty_ = true; }
  bool Contains(std::shared_ptr<BoundedValue> value) const { return buffer_.Contains(*value); }
  bool Overlap() const { return buffer_.Overlap(); }
  bool isDirty() const { return !buffer_.empty(); }
  void Absorb(std::shared_ptr<LevelNode> target) { 
    next_ = target->next_;
    buffer_.AddAll(target->buffer_);
    statistics_dirty_ = true;
  }
  void GetStringLog(std::vector<std::string>& set) {
    GetTreeStats()->GetStringLog(set);
    buffer_.GetStringLog(set);
  }

};

}