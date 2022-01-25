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
  // next node of this level.
  std::shared_ptr<SBSNode> next_;
  // files that stored in this level.
  TypeBuffer buffer_;
  // statistics info.
  bool statistics_dirty_;
  std::shared_ptr<Statistics> tree_stats_;
  // temp variables.

  // Build blank node.
  LevelNode(std::shared_ptr<StatisticsOptions> stat_options, std::shared_ptr<SBSNode> next) 
  : next_(next), 
    buffer_(), 
    statistics_dirty_(true),
    tree_stats_(std::make_shared<Statistics>(stat_options, stat_options->NowTimeSlice())) {}

  void Add(std::shared_ptr<BoundedValue> value) { buffer_.Add( value); tree_stats_->MergeStatistics(value); }
  std::shared_ptr<BoundedValue> Del(std::shared_ptr<BoundedValue> value) { 
    auto res = buffer_.Del(value->Identifier()); 
    statistics_dirty_ = true;
    return res;
  }
  bool Contains(std::shared_ptr<BoundedValue> value) const { return buffer_.Contains(value->Identifier()); }
  bool Overlap() const { return buffer_.Overlap(); }
  bool isDirty() const { return !buffer_.empty(); }
  bool isStatisticsDirty() const { return statistics_dirty_; }
  void Absorb(std::shared_ptr<LevelNode> target) { 
    next_ = target->next_;
    buffer_.AddAll(target->buffer_);
    statistics_dirty_ = true;
  }
  
  virtual void GetStringSnapshot(std::vector<KVPair>& set) const override {
    //if (!isStatisticsDirty());
    if (tree_stats_)
      tree_stats_->GetStringSnapshot(set);
    buffer_.GetStringSnapshot(set);
  }
  

};

}