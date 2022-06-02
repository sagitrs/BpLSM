#pragma once

#include <atomic>
#include <vector>
#include <stack>
#include <memory>
#include <array>
#include "db/dbformat.h"
#include "bounded.h"
#include "bounded_value_container.h"
#include "options.h"
#include "statistics.h"
namespace sagitrs {
struct SBSNode;
typedef BFileVec TypeBuffer;

enum TableVariableName : uint32_t {
  // stats.
  LocalGet,
  LocalWrite,
  LocalIterate,
  LocalLeaf,
  // files.
  HoleFileSize,
  TapeFileSize,
  TotalFileSize,
  HoleFileCount,
  TapeFileCount,
  TotalFileCount,
  HoleFileRuns,
  TapeFileRuns,
  TotalFileRuns,
  // scores.
  FileSizeScore = 1,
  FileRunScore,
  FileNumScore,
  FileDynamicScore,
  NodeWidthScore,

  TableVariableMax,
};

struct LevelNode : public Printable {
  // next node of this level.
  std::atomic<SBSNode*> next_;
  // files that stored in this level.
  TypeBuffer buffer_;
  // temp variables.
  struct VariableTable : public Printable, public std::array<uint64_t, TableVariableMax> {
    // statistics info.
   private:
    //bool stats_dirty_;
   public:
    uint64_t update_time_;
    Statistics *stats_; 
    BFile* hottest_;
    double max_runs_;

    VariableTable(const StatisticsOptions& stat_options) :
      std::array<uint64_t, TableVariableMax>(),
      update_time_(0),
      stats_(nullptr), 
      hottest_(nullptr),
      max_runs_(0) {}

    // Copy
    VariableTable(const VariableTable& table) :
      std::array<uint64_t, TableVariableMax>(table),
      stats_(nullptr),
      update_time_(0),
      hottest_(nullptr),
      max_runs_(0) {}

    virtual ~VariableTable() { SetDirty(); }
    //bool isDirty() const { return stats_dirty_; }
    void ResetVariables() {
      for (auto i = begin(); i != end(); ++i)
        *i = 0;
    }
    void SetDirty(bool state = true) { 
      if (stats_) {
        delete stats_;
        stats_ = nullptr;
      } 
    }
    //Statistics* TreeStatistics() { return stats_; }

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
  LevelNode(const StatisticsOptions& stat_options, 
            SBSNode* next) 
  : next_(next), 
    buffer_(), 
    table_(stat_options) {}
  // Copy existing node.
  LevelNode(const LevelNode& node):
    next_(node.next_.load(std::memory_order_relaxed)),
    buffer_(node.buffer_),
    table_(node.table_) {}
  ~LevelNode() {}

  void ReleaseAll() {
    for (auto file : buffer_)
      delete file;
  }
  void Add(BFile* value) {
    buffer_.Add(value); 
    table_.SetDirty();
    //table_.tree_->MergeStatistics(*value); 
  }
  BFile* Pop(const BFile& value) { 
    // warning: memory leak.
    auto res = buffer_.Pop(value.Identifier()); 
    table_.SetDirty();
    return res;
  }
  bool Contains(const BFile& value) const { 
    return buffer_.Contains(value.Identifier()); }
  bool Overlap() const { return buffer_.Overlap(); }
  bool isDirty() const { return !buffer_.empty(); }
  //bool isStatisticsDirty() const { return table_.isDirty(); }
  void Absorb(LevelNode* target) { 
    next_.store(target->next_, std::memory_order_relaxed);
    //next_ = target->next_;
    buffer_.AddAll(target->buffer_);
    table_.SetDirty();
  }
  
  virtual void GetStringSnapshot(std::vector<KVPair>& set) const override {
    table_.GetStringSnapshot(set);
    buffer_.GetStringSnapshot(set);
  }

};

}