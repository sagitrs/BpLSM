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
  // temp variables.
  struct VariableTable : public Printable {
    // statistics info.
   private:
    bool stats_dirty_;
    std::shared_ptr<Statistics> stats_; 
   public:
    uint64_t update_time_;
    std::shared_ptr<BoundedValue> hottest_;
    
    uint64_t max_runs_;

    VariableTable(std::shared_ptr<StatisticsOptions> stat_options) :
      stats_dirty_(true),
      stats_(std::make_shared<Statistics>(stat_options, stat_options->NowTimeSlice())), 
      update_time_(0),
      hottest_(nullptr),
      max_runs_(0) {}

    bool isDirty() const { return stats_dirty_; }
    void SetDirty(bool state = true) { stats_dirty_ = state; }
    std::shared_ptr<Statistics>& TreeStatistics() { return stats_; }

    size_t MaxRuns() const { return max_runs_; }

    virtual void GetStringSnapshot(std::vector<KVPair>& set) const override {
      //if (!isStatisticsDirty());
      if (stats_)
        stats_->GetStringSnapshot(set);
      if (hottest_) {
        set.emplace_back("UTime", std::to_string(update_time_));
        set.emplace_back("KSGet", std::to_string(hottest_->GetStatistics(KSGetCount, update_time_)));
      }
    }
  };
  VariableTable table_;

  // Build blank node.
  LevelNode(std::shared_ptr<StatisticsOptions> stat_options, std::shared_ptr<SBSNode> next) 
  : next_(next), 
    buffer_(), 
    table_(stat_options) {}

  void Add(std::shared_ptr<BoundedValue> value) {
    buffer_.Add(value); 
    table_.TreeStatistics()->MergeStatistics(value); 
  }
  std::shared_ptr<BoundedValue> Del(std::shared_ptr<BoundedValue> value) { 
    auto res = buffer_.Del(value->Identifier()); 
    table_.SetDirty();
    return res;
  }
  bool Contains(std::shared_ptr<BoundedValue> value) const { return buffer_.Contains(value->Identifier()); }
  bool Overlap() const { return buffer_.Overlap(); }
  bool isDirty() const { return !buffer_.empty(); }
  bool isStatisticsDirty() const { return table_.isDirty(); }
  void Absorb(std::shared_ptr<LevelNode> target) { 
    next_ = target->next_;
    buffer_.AddAll(target->buffer_);
    table_.SetDirty();
  }
  
  virtual void GetStringSnapshot(std::vector<KVPair>& set) const override {
    table_.GetStringSnapshot(set);
    buffer_.GetStringSnapshot(set);
  }

};

}