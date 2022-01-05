#pragma once

#include "bounded.h"
#include <memory>
#include <set>

namespace sagitrs {


typedef std::vector<std::shared_ptr<BoundedValue>> BoundedValueContainerBaseType;

struct BoundedValueContainer : public BoundedValueContainerBaseType, 
                               public BRealBounded {
  BoundedValueContainer() :   // Copy function.
    BoundedValueContainerBaseType(),
    BRealBounded("Undefined", "Undefined") {}

  BoundedValueContainer(const BoundedValueContainerBaseType& container) :   // Copy function.
    BoundedValueContainerBaseType(container),
    BRealBounded("Undefined", "Undefined") { Rebound(); }

  int Compare(const std::shared_ptr<BoundedValue> &a, const std::shared_ptr<BoundedValue> &b) const {
    int cmp = a->Min().compare(b->Min());
    if (cmp == 0) cmp = a->Max().compare(b->Max());
    return cmp;
  }
  void Add(std::shared_ptr<BoundedValue> value) { 
    if (size() > 1)
      Extend(*value);
    else
      BRealBounded::Rebound(*value);

    if (empty()) 
      push_back(value);
    else if (Compare(value, at(0)) <= 0)
      insert(begin(), value);
    else {
      auto prev = begin();
      for (auto i = prev+1; i != end(); ++i)
        if (Compare(*prev, value) <= 0 && Compare(value, *i) <= 0) {
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
  }
  bool Del(const BoundedValue& value) {
    for (auto iter = begin(); iter != end(); ++iter) 
      if ((*iter)->Identifier() == value.Identifier()) {
        erase(iter);
        if (OnBound(value)) Rebound();
        return 1;
      }
    return 0;
  }
  bool Contains(const BoundedValue& value) const {
    for (auto iter = begin(); iter != end(); ++iter) 
      if ((*iter)->Identifier() == value.Identifier()) 
        return 1;
    return 0;
  }
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
      BRealBounded::Rebound("Undefined", "Undefined"); 
      return; 
    }
    auto iter = begin();
    BRealBounded::Rebound(*(*iter));
    for (iter ++;iter != end(); iter ++) 
      Extend(*(*iter)); 
  }

  void GetInfo(std::vector<std::string>& set) const {
    for (auto i = begin(); i != end(); ++i)
      set.push_back(std::to_string((*i)->Identifier()));
  }
  std::string ToString() const {
    std::vector<std::string> set;
    GetInfo(set);
    std::stringstream ss;
    for (auto s : set) { ss << s << ";"; }
    return ss.str();
  }
};

}