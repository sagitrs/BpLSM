#pragma once

#include <vector>
#include <stack>
#include "sbs_node.h"
#include "sbs_iterator.h"
namespace sagitrs {
struct Scorer;

struct LeveledScorer : public Scorer {
 private:
  size_t max_level0_size_ = 8;
  size_t max_tiered_runs_ = 1;
  size_t base_children_ = 10;
  bool level0_found_ = 0;
  const size_t allow_seek_ = (uint64_t)(2) * 1024 * 1024 / 16384U;
 public:
  LeveledScorer() {}
  using Scorer::Init;
  using Scorer::Reset;
  using Scorer::Update;
  using Scorer::MaxScore;
 private: // override this function.
  virtual double Calculate() override { return CalculateByBufferSize(BufferSize()); }
 private:
  virtual double CalculateByBufferSize(size_t buffer_size) {
    double score = 0;
    if (Height() == 0) return 0;
    
    double buffer_score = 0;
    {
      if (!level0_found_ && buffer_size > 0) {
        level0_found_ = true;
        buffer_score = 1.0 * buffer_size / (max_level0_size_ + 1);
      } else {
        buffer_score = 1.0 * buffer_size / (max_tiered_runs_ + 1); 
      }
    }
    double get_score = 0;
    {
      int64_t leaves = GetStatistics(TypeHistorical, LeafCount);
      //int64_t global_leaves = Global().global_stats_->Get(TypeHistorical, LeafCount);
      int64_t get = GetStatistics(TypeHistorical, GetCount);
      //int64_t global_get = Global().global_stats_->Get(TypeHistorical, GetCount);
      if (buffer_size >= 1 && get >= allow_seek_ * buffer_size) {
        get_score = 1;
      }
    }

    score = buffer_score + get_score;

    if (score > 1) score = 1;
    if (score < 0) score = 0;

    return score;
  }
  bool CalculateIfFull(std::shared_ptr<SBSNode> node, size_t height) {
    Scorer::SetNode(node, height);
    return CalculateByBufferSize(BufferSize() + 1) >= 1.0;
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

  void Put(TypeValuePtr value, bool buffered = false) {
    iter_.SeekToRoot();
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    if (buffered) { iter_.AddBuffered(*options_, target); return; }
    iter_.Add(*options_, target);
    //iter.TargetIncStatistics(value->Min(), DefaultCounterType::PutCount, 1);                          // Put Statistics.
  }
  void BufferClear() {
    BoundedValueContainer container;
    iter_.SeekToRoot();
    for (auto range : container)
      iter_.Del(*options_, range);
    for (auto range : container)
      iter_.Add(*options_, range);
  }
  bool Contains(TypeValuePtr value) {
    iter_.SeekToRoot();
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    return iter_.SeekBoundedValue(target);
  }
  int SeekHeight(const Bounded& range) {
    iter_.SeekToRoot();
    iter_.SeekRange(range, true);
    return iter_.Current().height_;
  }
  void Get(const Bounded& range, BoundedValueContainer& container, std::shared_ptr<Scorer> scorer = nullptr) {
    iter_.SeekToRoot();
    iter_.SeekRange(range);
    //std::cout << iter.ToString() << std::endl;
    auto bound = std::make_shared<BRealBounded>(range.Min(), range.Max());
    iter_.GetBufferOnRoute(container, bound, scorer);
  }
  bool Del(TypeValuePtr value) {
    iter_.SeekToRoot();
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    return iter_.Del(*options_, target);
  }
  void PickFilesByIterator(int& height, BoundedValueContainer* containers) {
    height = iter_.Current().height_;
    if (containers != nullptr) {
      iter_.GetBufferInCurrent(containers[0]);
      assert(height > 0);
      if (height > 1)
        iter_.GetChildGuardInCurrent(containers[2]);
      for (iter_.Dive(); 
           iter_.Valid() && iter_.Current().node_->Guard().compare(containers[0].Max()) <= 0; 
           iter_.Next()) {
        iter_.GetBufferInCurrent(containers[1]);
      } 
    }
  }
  double PickFilesByScore(std::shared_ptr<Scorer> scorer, int& height, 
                        BoundedValueContainer* containers = nullptr) {
    iter_.SeekToRoot();
    double max_score = iter_.SeekScore(scorer);
    height = iter_.Current().height_;
    if (containers)
      PickFilesByIterator(height, containers);
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
    iter.SeekToRoot();
    size_t H = iter.Current().height_;

    for (int h = H; h >= 0; --h) {
      iter.SeekToRoot();
      for (iter.Dive(H - h); iter.Valid(); iter.Next()) {
        total += iter.Current().Buffer().size();
      }
    }
    return total;

  }
  std::shared_ptr<SBSNode> GetHead() const { return head_; }
};

}