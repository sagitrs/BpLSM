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
//TODO: Buffer shall be sorted!!!!!

struct LevelNode {
  std::shared_ptr<SBSNode> next_;
  TypeBuffer buffer_;
  // Statistics stats_;
  // ParaTable paras_;
  
  LevelNode() : next_(nullptr), buffer_() {}
  LevelNode(std::shared_ptr<SBSNode> next) 
  : next_(next), buffer_() {}
  void Add(std::shared_ptr<BoundedValue> value) { buffer_.Add(value); }
  void Del(std::shared_ptr<BoundedValue> value) { buffer_.Del(*value); }
  bool Contains(std::shared_ptr<BoundedValue> value) const { return buffer_.Contains(*value); }
  bool Overlap() const { return buffer_.Overlap(); }
  void Absorb(const LevelNode& node) {
    next_ = node.next_;
    buffer_.AddAll(node.buffer_);
  }
  bool isDirty() const { return !buffer_.empty(); }
};

}