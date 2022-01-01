#pragma once

#include <vector>
#include <stack>
#include "sbs_node.h"
namespace sagitrs {

template<typename TypeBoundedValue>
struct SBSkiplist {
 typedef std::shared_ptr<TypeBoundedValue> TypeValuePtr;
 private:
  SBSOptions options_;
  std::shared_ptr<SBSNode> head_;
 public:
  SBSkiplist() 
  : options_(),
    head_(std::make_shared<SBSNode>()) {}

  void Put(TypeValuePtr value) {
    auto iter = SBSNode::Iterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.SeekTree(*target);
    iter.Add(options_,  target);
  }
  bool Contains(TypeValuePtr value) {
    auto iter = SBSNode::Iterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.SeekTree(*target);
    return iter.Seek(target);
  }
  bool Del(TypeValuePtr value) {
    auto iter = SBSNode::Iterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.SeekTree(*target);
    bool contains = iter.Seek(target);
    if (!contains) return 0;
    iter.Del(options_, target);
    return 1;
  }
  std::string ToString() const {
    std::stringstream ss;
    for (auto i = head_; i != nullptr; i = i->Next(0)) {
      ss << i->ToString();
      ss << "-----------------------------------------------------------------" << std::endl;
    }
    ss << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++";
    return ss.str();
  }
};

}