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
  std::shared_ptr<Statistics> node_stats_, child_stats_;
  
  // Build blank node.
  LevelNode(std::shared_ptr<StatisticsOptions> stat_options, std::shared_ptr<SBSNode> next = nullptr) 
  : next_(next), 
    buffer_(), 
    node_stats_(std::make_shared<Statistics>(stat_options)),
    child_stats_() {}

  void Add(std::shared_ptr<BoundedValue> value, bool is_head) { 
    buffer_.Add(value); 
    //if (!is_head)
    node_stats_->SetGuard(buffer_.Min()); 
  }
  void Del(std::shared_ptr<BoundedValue> value, bool is_head) { 
    buffer_.Del(*value); 
    if (!buffer_.empty())// && !is_head) 
      node_stats_->SetGuard(buffer_.Min()); 
  }
  bool Contains(std::shared_ptr<BoundedValue> value) const { return buffer_.Contains(*value); }
  bool Overlap() const { return buffer_.Overlap(); }
  bool isDirty() const { return !buffer_.empty(); }

  void Absorb(std::shared_ptr<LevelNode> node) {
    next_ = node->next_;
    buffer_.AddAll(node->buffer_);
    node_stats_->Superposition(*(node->node_stats_));
    //assert(node_stats_->Get(TypeHistorical, LeafCount) == );
  }
  void GetStringLog(std::vector<std::string>& set) {
    node_stats_->GetStringLog(set);
    set.push_back("CHILDSTATS");
    if (child_stats_) 
      child_stats_->GetStringLog(set);
    buffer_.GetStringLog(set);
  }

};

}