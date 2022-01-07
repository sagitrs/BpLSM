#pragma once

#include <vector>
#include <stack>
#include "sbs_node.h"
#include "sbs_iterator.h"
namespace sagitrs {
struct Scorer;

struct LeveledScorer : public Scorer {
 private:
  struct GlobalStatus {
    size_t head_height_;
  } status_;
  void Init(std::shared_ptr<SBSNode> head) {  status_.head_height_ = head->Height();  }
  size_t max_level0_size_ = 8;
  size_t max_tiered_runs_ = 1;
  size_t base_children_ = 10;

  bool level0_found_ = false;
 public:
  LeveledScorer(std::shared_ptr<SBSNode> head) { Init(head); }
  virtual double CalculateByBufferSize(std::shared_ptr<SBSNode> node, size_t height, size_t buffer_size) {
    Scorer::SetNode(node, height);
    double score = 0;
    if (!level0_found_) {
      score = buffer_size > max_level0_size_ ? 1 : 0;
    } else {
      double rest = (0.0 + max_tiered_runs_ - buffer_size) / max_tiered_runs_;
      if (rest < 0) 
        score = 1;
      else {
        double scale = 1.0 * Width() / base_children_;
        score = 1.0 - rest * scale;
      }
    }
    if (score > 1) score = 1;
    if (score < 0) score = 0;

    if (buffer_size > 0) {
      level0_found_ = 1;
    }
    return score;
  }
  virtual double Calculate(std::shared_ptr<SBSNode> node, size_t height) override {
    Scorer::SetNode(node, height);
    return CalculateByBufferSize(node, height, BufferSize());
  }
  bool CalculateIfFull(std::shared_ptr<SBSNode> node, size_t height) {
    Scorer::SetNode(node, height);
    return CalculateByBufferSize(node, height, BufferSize() + 1) == 1;
  }
};

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
    head_(std::make_shared<SBSNode>(options_, 6)),
    iter_(head_) {}

  void Put(TypeValuePtr value) {
    auto iter = SBSIterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.Add(*options_, target);
    iter.TargetIncStatistics(DefaultCounterType::PutCount, 1);
  }
  bool Contains(TypeValuePtr value) {
    auto iter = SBSIterator(head_);
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    iter.SeekRange(*target);
    return iter.SeekValue(target);
  }
  int SeekHeight(const Bounded& range) {
    auto iter = SBSIterator(head_);
    iter.SeekRange(range, true);
    return iter.Height();
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
    iter.Del(*options_, target);
    return 1;
  }
  double PickFilesByScore(std::shared_ptr<Scorer> scorer, int& height, 
                        BoundedValueContainer* container = nullptr) {
    auto iter = SBSIterator(head_);
    double max_score = iter.SeekScore(scorer);
    height = iter.Height();

    if (container != nullptr) {
      iter.LoadRoute();
      iter.GetRangesInCurrent(container[0]);

      assert(height != 0);
      for (iter.JumpDown(); iter.Valid() && iter.Guard().compare(container[0].Max()) <= 0; iter.JumpNext()) {
        iter.GetRangesInCurrent(container[1]);
        iter.GetChildGuardInCurrent(container[2]);
      } 
    }
    return max_score;
  }
  std::string ToString() const {
    std::stringstream ss;
    for (auto i = head_; i != nullptr; i = i->Next(0)) {
      ss << i->ToString();
    }
    ss << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++";
    return ss.str();
  }
  size_t size() const {
    size_t total = 0;
    auto iter = SBSIterator(head_);
    iter.ResetToRoot();
    size_t H = iter.Height();
    for (int h = H; h >= 0; --h) {
      iter.ResetToRoot();
      for(size_t i = 0; i < H - h; ++i)
        iter.JumpDown();
      for (; iter.Valid(); iter.JumpNext()) {
        total += iter.BufferSize();
      }
    }
    return total;

  }
  std::shared_ptr<SBSNode> GetHead() const { return head_; }
};

}