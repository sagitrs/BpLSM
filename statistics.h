#pragma once

#include <map>
#include <array>
#include "options.h"
#include "leveldb/env.h"

namespace sagitrs {

enum DefaultCounterType : uint32_t {
  LeafCount = 0,                        // 1 for leaf and 0 for non-leaf, only tree-stat has meaning. 
  GetCount,                             // +1 for all route node when querying, split by tree::leaf_count, merge when absorbing.
                                        // for middle node: split by tree::leaf_count, merge when absorbing.
                                        // for leaf node: destory when absorbing, construct by parent node.
  PutCount,
  AbsorbCount,
  SplitCount,
  RangeSeekCount,
  QueryHitCount,
  QueryCacheHitCount,
  DefaultCounterTypeMax
};

struct Counter {
  typedef uint32_t TypeLabel;
  typedef size_t TypeData;
 private:
  std::array<TypeData, DefaultCounterTypeMax> list_;
 public:
  TypeData operator[](size_t k) const { return list_[k]; }
  virtual void Inc(TypeLabel label, int size) { list_[label] += size; }
  virtual void Set(TypeLabel label, TypeData size) { list_[label] = size; }
  virtual void Scale(double k) {
    for (auto& element : list_)
      element *= k; 
  }
  virtual void Superposition(const Counter& target) {
    for (size_t i = 0; i < DefaultCounterTypeMax; ++i)
      list_[i] += target[i];
  }
  void GetInfo(std::vector<std::string>& set) {
    //set.push_back("RQ =" + std::to_string(list_[RangeSeekCount]));
    //set.push_back("PQ =" + std::to_string(list_[PointSeekCount]));
    //set.push_back("HIT=" + std::to_string(list_[QueryHitCount]));
    set.push_back("COUNT=" + std::to_string(list_[PutCount]));
    set.push_back("PUT=" + std::to_string(list_[PutCount]));
    set.push_back("GET=" + std::to_string(list_[GetCount]));
    set.push_back("EAT=" + std::to_string(list_[AbsorbCount]));
    set.push_back("SPL=" + std::to_string(list_[SplitCount]));
  }
};

struct Statistics {
  typedef std::shared_ptr<Counter> CPTR;
  typedef uint64_t TypeTime;
  std::shared_ptr<StatisticsOptions> options_;
 private:
  std::map<TypeTime, CPTR> history_;
  uint64_t Now() { return options_->TimerEnv()->NowMicros() / options_->TimeSliceMicroSecond(); }
  uint64_t LastTimeSlice() const { return history_.rbegin()->first; }
  CPTR Current() {
    TypeTime now = Now();
    if (history_.empty() || now > LastTimeSlice()) {
      CPTR tmp = std::make_shared<Counter>();
      history_[now] = tmp;
      CleanUpSnapshot();
    }
    return history_.rbegin()->second;
  }

  void CleanUpSnapshot() {
    TypeTime now = Now();
    while (!history_.empty()) {
      auto head = *history_.begin();
      TypeTime time = head.first;
      CPTR node = head.second;
      if (time + options_->TimeSliceMaximumSize() < now) {
        history_.erase(time);
      } else {
        break;
      }
    }
  }
 public:
  Statistics(std::shared_ptr<StatisticsOptions> options) 
  : options_(options), history_() {
    Inc(AbsorbCount, 1);
  }
  Statistics(const Statistics& stats) = delete;
  //: options_(stats.options_), history_(stats.history_) {
  //  Current()->Set(AbsorbCount, 1);
  //}
  void Inc(Counter::TypeLabel label, int size) { Current()->Inc(label, size); }
  Counter::TypeData GetCurrent(Counter::TypeLabel label) { return (*Current())[label]; }

  // When a node inherits the contents of the original node in proportion k, 
  // ParaTable must give an initialization scheme.
  virtual void Shrink(double k) {
    for (auto& kv : history_) {
      if (kv.first != AbsorbCount)
        kv.second->Scale(k);
    }
  }
  // When a node merges another node, ParaTable must give a merging scheme.
  virtual void Absorb(const Statistics& bro) {
    for (auto& kv : bro.history_) {
      auto p = history_.find(kv.first);
      if (p == history_.end()) 
        history_[kv.first] = std::make_shared<Counter>(*kv.second);
      else
        p->second->Superposition(*kv.second);
    }
  }

  void GetInfo(std::vector<std::string>& set) {
    Current()->GetInfo(set);
  }
  //void IncStatistics(Counter::TypeLabel label, size_t size) { Inc(label, size); }
};

}