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
  std::shared_ptr<SBSOptions> options_;
  std::shared_ptr<TypeNode> head_;
  SBSIterator iter_;
 public:
  SBSkiplist(const SBSOptions& options) 
  : options_(std::make_shared<SBSOptions>(options)),
    head_(std::make_shared<SBSNode>(options_, 7)),
    iter_(head_) {}

  void Put(TypeValuePtr value) {
    auto iter = SBSIterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.SeekRange(*target);
    iter.Add(*options_, target);
    iter.TargetIncStatistics(DefaultCounterType::PutCount, 1);
  }
  bool Contains(TypeValuePtr value) {
    auto iter = SBSIterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.SeekRange(*target);
    return iter.SeekValue(target);
  }
  void Get(const Bounded& range, BoundedValueContainer& container) {
    auto iter = SBSIterator(head_);
    iter.SeekRange(range);
    //std::cout << iter.ToString() << std::endl;
    auto bound = std::make_shared<BRealBounded>(range.Min(), range.Max());
    iter.GetRangesOnRoute(container, bound);
  }
  bool Del(TypeValuePtr value) {
    auto iter = SBSIterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.SeekRange(*target);
    bool contains = iter.SeekValue(target);
    if (!contains) return 0;
    iter.Del(*options_, target);
    return 1;
  }
  void PickFilesByScore(std::shared_ptr<Scorer> scorer, BoundedValueContainer& container) {
    auto iter = SBSIterator(head_);
    iter.SeekScore(scorer);
    iter.GetRangesInNode(container);
  }
  std::string ToString() const {
    std::stringstream ss;
    for (auto i = head_; i != nullptr; i = i->Next(0)) {
      ss << i->ToString();
    }
    ss << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++";
    return ss.str();
  }
  std::shared_ptr<SBSNode> GetHead() const { return head_; }
};

}