#pragma once

#include "bounded.h"
#include <memory>
#include <set>

namespace sagitrs {


typedef std::vector<std::shared_ptr<BoundedValue>> BoundedValueContainerBaseType;

struct BoundedValueContainer : public BoundedValueContainerBaseType, 
                               public RealBounded,
                               public Printable {
  bool stats_dirty_;
  std::shared_ptr<Statistics> stats_;
  void SetStatsDirty() { stats_dirty_ = true; }
  std::shared_ptr<Statistics> GetStatistics() {
    if (stats_dirty_) {
      stats_ = nullptr;
      for (auto i = begin(); i != end(); ++i) {
        if (stats_) stats_->MergeStatistics(*i);
        else {
          auto sa = std::dynamic_pointer_cast<Statistable>(*i);
          auto st = std::dynamic_pointer_cast<Statistics>(sa);
          stats_ = std::make_shared<Statistics>(*st);
        }
      }
      stats_dirty_ = false;
    }
    return stats_;
  }




  BoundedValueContainer() :   // Copy function.
    BoundedValueContainerBaseType(),
    RealBounded("Undefined", "Undefined") {}

  BoundedValueContainer(const BoundedValueContainerBaseType& container) :   // Copy function.
    BoundedValueContainerBaseType(container),
    RealBounded("Undefined", "Undefined") { Rebound(); }

  static int StaticCompare(const std::shared_ptr<BoundedValue> &a, const std::shared_ptr<BoundedValue> &b) {
    int cmp = a->Min().compare(b->Min());
    if (cmp == 0) cmp = a->Max().compare(b->Max());
    return cmp;
  }
  void Add(std::shared_ptr<BoundedValue> value) { 
    // bound adjust.
    if (size() > 0)
      RealBounded::Extend(*value);
    else
      RealBounded::Rebound(*value);
    // statistics adjust.
    stats_dirty_ = true;

    if (empty()) 
      push_back(value);
    else if (StaticCompare(value, *begin()) <= 0)
      insert(begin(), value);
    else {
      auto prev = begin();
      for (auto i = prev+1; i != end(); ++i)
        if (StaticCompare(*prev, value) <= 0 && StaticCompare(value, *i) <= 0) {
          insert(i, value);
          return;
        } else {
          prev = i;
        }
      push_back(value);
    } 
  }
  void AddAll(const BoundedValueContainer& b) {
    for (auto value : b) { Add(value); }
    // statistics adjust.
    stats_dirty_ = true;
  }
  std::shared_ptr<BoundedValue> Del(uint64_t id) {
    auto iter = Locate(id);
    if (iter == end()) return nullptr;
    // statistics adjust.
    stats_dirty_ = true;

    auto res = *iter;
    erase(iter);
    if (OnBound(*res)) Rebound();
    return res;
  }
  bool Contains(uint64_t id) const { return Locate(id) != end(); }
  std::shared_ptr<BoundedValue> Get(uint64_t id) { return *Locate(id); }
  bool Overlap() const {
    auto i = begin();
    auto prev = i;
    for (++i; i != end(); ++i) {
      if ((*i)->Min().compare((*prev)->Max()) <= 0) 
        return 1;
      else 
        prev = i;
    }
    return 0;
  }
  void Rebound() {
    if (size() == 0) { 
      RealBounded::Rebound("Undefined", "Undefined"); 
      return; 
    }
    auto iter = begin();
    RealBounded::Rebound(*(*iter));
    for (iter ++;iter != end(); iter ++) 
      Extend(*(*iter)); 
  }

  virtual void GetStringSnapshot(std::vector<KVPair>& snapshot) const override {
    snapshot.emplace_back("Min", Min().ToString());
    snapshot.emplace_back("Max", Max().ToString());
    for (size_t i = 0; i < size(); ++i)
      snapshot.emplace_back(
        "A["+std::to_string(i)+"]", 
        std::to_string(operator[](i)->Identifier())
         + "|" + 
        std::to_string(operator[](i)->GetStatistics(ValueGetCount, STATISTICS_ALL))
      );
  }
 private:
  const std::__wrap_iter<const std::shared_ptr<sagitrs::BoundedValue> *> Locate(uint64_t id) const {
    for (auto iter = begin(); iter != end(); ++iter) 
    if ((*iter)->Identifier() == id) 
      return iter;
    return end();
  } 
  
};

}