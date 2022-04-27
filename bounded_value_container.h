#pragma once

#include "bounded.h"
#include <memory>
#include <set>
#include "statistics.h"
#include "bfile.h"
namespace sagitrs {

typedef std::vector<BFile*> BFileVecBase;

struct BFileVec : public BFileVecBase, 
                  public RealBounded,
                  public Printable {
  //bool stats_dirty_;
  Statistics* stats_;
  void SetStatsDirty() { 
    if (stats_) {
      delete stats_; 
      stats_ = nullptr;
    } 
  }
  BFile* GetOne() const {
    if (BFileVecBase::size() != 1) return nullptr;
    return *begin();
  } 
  void UpdateOneFileStatistics(
    Statistable::TypeLabel label, 
    Statistable::TypeData diff, 
    Statistable::TypeTime time) {
    if (size() != 1) return;
    auto vp = GetOne();
    if (vp) vp->UpdateStatistics(label, diff, time);
  }
  Statistics* GetStatistics() {
    if (size() == 1) return GetOne();
    if (!stats_) SetStatsDirty();
    for (auto i = begin(); i != end(); ++i) {
      if (stats_) 
        stats_->MergeStatistics(**i);
      else
        stats_ = new Statistics(**i);
    }
    return stats_;
  }
  //-----------------------------------------------------------------
  BFileVec() :   // Copy function.
    BFileVecBase(),
    RealBounded("Undefined", "Undefined"),
    stats_(nullptr) {}

  BFileVec(const BFileVecBase& container) :   // Copy function.
    BFileVecBase(container),
    RealBounded("Undefined", "Undefined"),
    stats_(nullptr) { Rebound(); }

  ~BFileVec() { if (stats_) delete stats_; }

  static int StaticCompare(const BFile &a, const BFile &b) {
    int cmp = a.Min().compare(b.Min());
    if (cmp == 0) cmp = a.Max().compare(b.Max());
    return cmp;
  }
  void Add(BFile* value) { 
    // bound adjust.
    if (size() > 0)
      RealBounded::Extend(*value);
    else
      RealBounded::Rebound(*value);
    // statistics adjust.
    SetStatsDirty();

    if (empty()) 
      push_back(value);
    else if (StaticCompare(*value, **begin()) <= 0)
      insert(begin(), value);
    else {
      auto prev = begin();
      for (auto i = prev+1; i != end(); ++i)
        if (StaticCompare(**prev, *value) <= 0 && StaticCompare(*value, **i) <= 0) {
          insert(i, value);
          return;
        } else {
          prev = i;
        }
      push_back(value);
    } 
  }
  void AddAll(const BFileVec& b) {
    for (auto value : b) { Add(value); }
    // statistics adjust.
    SetStatsDirty();
  }
  BFile* Del(uint64_t id) {
    auto iter = Locate(id);
    if (iter == end()) return nullptr;
    // statistics adjust.
    SetStatsDirty();

    auto res = *iter;
    erase(iter);
    if (OnBound(*res)) Rebound();
    return res;
  }
  bool Contains(uint64_t id) const { return Locate(id) != end(); }
  BFile* Get(uint64_t id) { return *Locate(id); }
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
      );
  }
  size_t GetValueWidth(BFile* value) {
    Slice a(value->Min()), b(value->Max());
    size_t width = 1;
    for (auto & child : *this)
      if (value->Include(*child) == BInclude)
        width ++;
    return width;
  }
 private:
  const BFileVecBase::const_iterator Locate(uint64_t id) const {
    for (auto iter = begin(); iter != end(); ++iter) 
    if ((*iter)->Identifier() == id) 
      return iter;
    return end();
  } 
  

};

}