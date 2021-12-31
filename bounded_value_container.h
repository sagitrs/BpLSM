#pragma once

#include "bounded.h"
#include <memory>
#include <set>

namespace sagitrs {
struct BoundedValuePtrCmpClass {
  bool operator() (const std::shared_ptr<BoundedValue> &a, const std::shared_ptr<BoundedValue> &b) const {
    return a->Min().compare(b->Min()) < 0;
  }
};

typedef std::set<std::shared_ptr<BoundedValue>, BoundedValuePtrCmpClass> BoundedValueContainerBaseType;

struct BoundedValueContainer : public BoundedValueContainerBaseType, 
                               public BRealBounded {
  BoundedValueContainer() :   // Copy function.
    BoundedValueContainerBaseType(),
    BRealBounded("Undefined", "Undefined") {}

  BoundedValueContainer(const BoundedValueContainerBaseType& container) :   // Copy function.
    BoundedValueContainerBaseType(container),
    BRealBounded("Undefined", "Undefined") { Rebound(); }

  void Add(std::shared_ptr<BoundedValue> value) { 
    insert(value); 
    if (size() > 1)
      Extend(*value);
    else
      BRealBounded::Rebound(*value);
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
    iterator i = begin();
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
};

}