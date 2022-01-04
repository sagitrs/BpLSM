#pragma once

#include <vector>
#include <stack>
#include "sbs_node.h"
#include "sbs_iterator.h"
namespace sagitrs {
struct Scorer;

struct SBSkiplist {
  friend struct Scorer;
  typedef std::shared_ptr<BoundedValue> TypeValuePtr;
  typedef SBSNode TypeNode;
 private:
  SBSOptions options_;
  std::shared_ptr<TypeNode> head_;
  SBSIterator iter_;
 public:
  SBSkiplist() 
  : options_(),
    head_(std::make_shared<SBSNode>()),
    iter_(head_) {}

  void Put(TypeValuePtr value) {
    auto iter = SBSIterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.TraceRoute(*target);
    iter.Add(options_,  target);
  }
  bool Contains(TypeValuePtr value) {
    auto iter = SBSIterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.TraceRoute(*target);
    return iter.Seek(target);
  }
  bool Del(TypeValuePtr value) {
    auto iter = SBSIterator(head_);
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