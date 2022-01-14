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

struct LevelNode : public Printable {
  std::shared_ptr<SBSNode> next_;
  TypeBuffer buffer_;

  bool statistics_dirty_;
  std::shared_ptr<Statistics> tree_stats_;
  
  // Build blank node.
  LevelNode(std::shared_ptr<StatisticsOptions> stat_options, std::shared_ptr<SBSNode> next = nullptr) 
  : next_(next), 
    buffer_(), 
    statistics_dirty_(true),
    tree_stats_(std::make_shared<Statistics>(stat_options)) {}

  void Add(std::shared_ptr<BoundedValue> value) { buffer_.Add( value); tree_stats_->MergeStatistics(value); }
  std::shared_ptr<BoundedValue> Del(std::shared_ptr<BoundedValue> value) { 
    auto res = buffer_.Del(*value); 
    statistics_dirty_ = true;
    return res;
  }
  bool Contains(std::shared_ptr<BoundedValue> value) const { return buffer_.Contains(*value); }
  bool Overlap() const { return buffer_.Overlap(); }
  bool isDirty() const { return !buffer_.empty(); }
  bool isStatisticsDirty() const { return statistics_dirty_; }
  void Absorb(std::shared_ptr<LevelNode> target) { 
    next_ = target->next_;
    buffer_.AddAll(target->buffer_);
    statistics_dirty_ = true;
  }
  
  virtual void GetStringSnapshot(std::vector<KVPair>& set) const override {
    assert(!isStatisticsDirty());
    tree_stats_->GetStringSnapshot(set);
    buffer_.GetStringSnapshot(set);
  }
  

};

}