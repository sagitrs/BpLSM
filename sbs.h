#pragma once

#include <vector>
#include <stack>
#include "sbs_node.h"
namespace sagitrs {
struct Scorer;
template<typename TypeBoundedValue, typename TypeScorer, typename TypeCounter>
struct SBSkiplist {
  friend struct Scorer;
  typedef std::shared_ptr<TypeBoundedValue> TypeValuePtr;
  typedef SBSNode<TypeCounter> TypeNode;
 private:
  SBSOptions options_;
  std::shared_ptr<TypeNode> head_;
  TypeNode::Iterator<TypeScorer> iter_;
 public:
  SBSkiplist() 
  : options_(),
    head_(std::make_shared<SBSNode>()) {}

  void Put(TypeValuePtr value) {
    auto iter = SBSNode::Iterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.TraceRoute(*target);
    iter.Add(options_,  target);
  }
  bool Contains(TypeValuePtr value) {
    auto iter = SBSNode::Iterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.TraceRoute(*target);
    return iter.Seek(target);
  }
  bool Del(TypeValuePtr value) {
    auto iter = SBSNode::Iterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.TraceRoute(*target);
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