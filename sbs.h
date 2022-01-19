#pragma once

#include <vector>
#include <stack>
#include "sbs_node.h"
#include "sbs_iterator.h"
#include "delineator.h"
namespace sagitrs {
struct Scorer;

struct LeveledScorer : public Scorer {
 private:
  size_t base_children_ = 10;
  const size_t allow_seek_ = (uint64_t)(2) * 1024 * 1024 / 16384U;
 public:
  LeveledScorer() {}
  using Scorer::Init;
  using Scorer::Reset;
  using Scorer::Update;
  using Scorer::MaxScore;
  using Scorer::GetScore;
  using Scorer::ValueScore;
 private: // override this function.
  virtual double ValueCalculate(std::shared_ptr<BoundedValue> value) override { 
    return std::max(0.1, 1.0 * value->GetStatistics(ValueGetCount, STATISTICS_ALL) / allow_seek_); 
  }
};

struct SBSkiplist {
  friend struct Scorer;
  typedef std::shared_ptr<BoundedValue> TypeValuePtr;
  typedef SBSNode TypeNode;
  std::shared_ptr<SBSOptions> options_;
 private:
  std::shared_ptr<TypeNode> head_;
  SBSIterator iter_;
 public:
  SBSkiplist(const SBSOptions& options) 
  : options_(std::make_shared<SBSOptions>(options)),
    head_(std::make_shared<SBSNode>(options_, 6)),
    iter_(head_) {}
  void Reinsert() { iter_.Reinsert(*options_); }
  void Put(TypeValuePtr value, bool buffered = false) {
    iter_.SeekToRoot();
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    if (buffered) { iter_.AddBuffered(*options_, target); return; }
    iter_.Add(*options_, target);
    //iter.TargetIncStatistics(value->Min(), DefaultCounterType::PutCount, 1);                          // Put Statistics.
  }
  void BufferDel(BoundedValueContainer& container) {
    iter_.SeekToRoot();
    iter_.GetBuffered(container);
    for (auto range : container)
      iter_.Del(*options_, range);
    assert(iter_.Recycler().size() == container.size());
    iter_.Recycler().clear();
  }
  void AddAll(const BoundedValueContainer& container) {
    for (auto range : container)
      iter_.Add(*options_, range);
  }
  size_t BufferSize() { 
    BoundedValueContainer container;
    iter_.SeekToRoot();
    iter_.GetBuffered(container);
    return container.size();
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
    auto bound = std::make_shared<RealBounded>(range.Min(), range.Max());
    iter_.GetBufferOnRoute(container, bound);
  }
  void UpdateStatistics(std::shared_ptr<BoundedValue> value, uint32_t label, int64_t diff) {
    iter_.SeekToRoot();
    iter_.SeekRange(*value);
    auto target = iter_.SeekValueInRoute(value->Identifier());
    if (target == nullptr) {
      // file is deleted when bversion is unlocked.
      return;
    }
    target->UpdateStatistics(label, diff, options_->NowTimeSlice());
    iter_.SetRouteStatisticsDirty();
  }
  bool Del(TypeValuePtr value, bool auto_reinsert = true) {
    iter_.SeekToRoot();
    //auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    return iter_.Del(*options_, value, auto_reinsert);
  }
  void PickFilesByIterator(std::shared_ptr<Scorer> scorer, int& height, BoundedValueContainer* containers) {
    height = iter_.Current().height_;
    if (containers == nullptr)
      return;
    
    BoundedValueContainer& base_buffer = containers[0];
    BoundedValueContainer& child_buffer = containers[1];
    BoundedValueContainer& guards = containers[2];

    // get file in current.
    iter_.GetBufferInCurrent(base_buffer);    
    int rest = options_->MaxCompactionFiles();
    rest -= base_buffer.size();
    //if (height > 1)
    //  iter_.GetChildGuardInCurrent(containers[2]);
    std::deque<Coordinates> queue;
    for (iter_.Dive(); 
         iter_.Valid() && iter_.Current().node_->Guard().compare(base_buffer.Max()) <= 0; 
         iter_.Next()) {
      queue.push_back(Coordinates(iter_.Current()));
    }

    while (!queue.empty()) {
      bool ok = iter_.SeekNode(queue.front());
      assert(ok);
      queue.pop_front();
      
      bool load = false;
      if (iter_.Current().height_ == 0)
        load = true;
      else if (rest < iter_.Current().Buffer().size())
        load = false;
      else {
        double score = scorer->GetScore(iter_.Current().node_, iter_.Current().height_); 
        score += 1.0 / scorer->Capacity();
        load = (score >= options_->NeedsCompactionScore());
      }

      if (!load) {
        auto pacesetter = iter_.Current().node_->Pacesetter();
        if (pacesetter) guards.push_back(pacesetter);
      } else { 
        iter_.GetBufferInCurrent(child_buffer); 
        rest -= iter_.Current().Buffer().size();
        if (iter_.Current().height_ > 0) {
          auto ed = iter_.Current().NextNode().DownNode();
          for (iter_.Dive(); !(iter_.Current() == ed); iter_.Next())
            queue.push_back(Coordinates(iter_.Current()));
        }
      }
    }
  }
  double PickFilesByScore(std::shared_ptr<Scorer> scorer, double baseline,
                          int& height, BoundedValueContainer* containers = nullptr) {
    iter_.SeekToRoot();
    double max_score = iter_.SeekScore(scorer, baseline, true);
    height = iter_.Current().height_;
    if (containers)
      PickFilesByIterator(scorer, height, containers);
    return max_score;
  }
  bool HasScore(std::shared_ptr<Scorer> scorer, double baseline) {
    iter_.SeekToRoot();
    iter_.SeekScore(scorer, baseline, false);
    return scorer->isUpdated();
  }
 private:
  void PrintDetailed(std::ostream& os) const {
    os << "----------Print Detailed Begin----------" << std::endl;
    head_->ForceUpdateStatistics();
    for (auto i = head_; i != nullptr; i = i->Next(0))
      os << i->ToString();
    os << "----------Print Detailed End----------" << std::endl;  
  }
  void PrintSimple(std::ostream& os) const {
    auto iter = std::make_shared<SBSIterator>(head_);
    std::vector<size_t> hs;
    size_t maxh = 0;
    os << "----------Print Simple Begin----------" << std::endl;
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next()) {
      auto height = iter->Current().node_->Height();
      hs.push_back(height);
      if (height > maxh) maxh = height;
    }
    for (int h = maxh; h > 0; --h) {
      for (size_t i = 0; i < hs.size(); ++i)
        os << (hs[i] >= h ? '|' : ' ');
      os << std::endl;
    }
    os << "----------Print Simple End----------" << std::endl;
  }
  void PrintStatistics(std::ostream& os) const {
    os << "----------Print Statistics Begin----------" << std::endl;
    Delineator d;
    auto iter = std::make_shared<SBSIterator>(head_);
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next())
      d.AddStatistics(iter->Current().node_->Guard(), iter->GetRouteMergedStatistics());
    d.PrintTo(os, options_->NowTimeSlice(), KSGetCount);
    os << "----------Print Statistics End----------" << std::endl;
  }
 public:
  std::string ToString() const {
    std::stringstream ss;
    PrintSimple(ss);
    PrintDetailed(ss);
    PrintStatistics(ss);
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
  std::vector<SBSNode::ValuePtr>& Recycler() { return iter_.Recycler(); }
};

}