#pragma once

#include <vector>
#include <stack>
#include <algorithm>
#include <math.h>
#include "sbs_node.h"
#include "sbs_iterator.h"
#include "delineator.h"

#include "scorer_impl.h"
namespace sagitrs {
struct Scorer;
struct SBSkiplist {
  friend struct Scorer;
  typedef std::shared_ptr<BoundedValue> TypeValuePtr;
  typedef SBSNode TypeNode;
  std::shared_ptr<SBSOptions> options_;
 private:
  std::shared_ptr<TypeNode> head_;
  SBSIterator iter_;
 public:
  SBSkiplist(std::shared_ptr<SBSOptions> options) 
  : options_(options),
    head_(std::make_shared<SBSNode>(options_, 6)),
    iter_(head_) {}
  void Reinsert() { iter_.Reinsert(*options_); }
  std::shared_ptr<SBSIterator> NewIterator() const { return std::make_shared<SBSIterator>(head_); }
  
  void Put(TypeValuePtr value) {
    bool state = PutBlocked(value);
    if (!state) {
      BoundedValueContainer container;
      assert(iter_.Current().TestState(*options_) > 0);
      iter_.Current().SplitNext(*options_, &container);
      for (auto &v : container) {
        PutBlocked(v);
      }
    }
  }
  bool PutBlocked(TypeValuePtr value) {
    iter_.SeekToRoot();
    auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    bool state = iter_.Add(*options_, target);
    return state;
    //iter.TargetIncStatistics(value->Min(), DefaultCounterType::PutCount, 1);                          // Put Statistics.
  }
  
  void AddAll(const BoundedValueContainer& container) {
    for (auto range : container)
      iter_.Add(*options_, range);
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
    Statistics::TypeTime now = options_->NowTimeSlice();
    target->UpdateStatistics(label, diff, now);
    iter_.SetRouteStatisticsDirty();
    //iter_.UpdateRouteHottest(target);
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
    BoundedValueContainer& l0guards = containers[3];

    // get file in current.
    iter_.GetBufferInCurrent(base_buffer);    
    int rest = options_->MaxCompactionFiles();
    rest -= base_buffer.size();

    //bool skip_first_level = iter_.Current().Width() < options_->MinWidth();
        //if (height > 1)
    //  iter_.GetChildGuardInCurrent(containers[2]);
    std::deque<Coordinates> queue;
    for (iter_.Dive(); 
         iter_.Valid() && iter_.Current().node_->Guard().compare(base_buffer.Max()) <= 0; 
         iter_.Next()) {
      //if (base_buffer.Include(iter_.Current().node_->Guard()))
      queue.push_back(Coordinates(iter_.Current()));
    }

    while (!queue.empty()) {
      bool ok = iter_.SeekNode(queue.front());
      assert(ok);
      queue.pop_front();
      
      size_t h = iter_.Current().height_;
      int files = h == 0 ? 0 : iter_.Current().Buffer().size();
      if (h == 1) 
        files += iter_.Current().Width();

      bool load = false;
      //else if (h + 1 == height && skip_first_level)
      //  load = true;
      if (base_buffer.Include(iter_.Current().node_->Guard()) == false)
        load = false;
      if (h == 0)
        load = true;
      else if (rest < files)
        load = false;
      else {
        if (rest < -10) {
          std::cout << "Warning : Compact too much files(" << rest << ")." << std::endl;
        }
        double score = scorer->GetScore(iter_.Current().node_, iter_.Current().height_); 
        score += 1.0 / scorer->Capacity();
        load = (score >= options_->NeedsCompactionScore());
      }

      auto pacesetter = iter_.Current().node_->Pacesetter();
      if (pacesetter) {
        if (h > 0) 
          guards.push_back(pacesetter);
        else if (h == 0)
          l0guards.push_back(pacesetter);
      }
      if (load) { 
        iter_.GetBufferInCurrent(child_buffer); 

        if (h > 0) {
          auto ed = iter_.Current().NextNode().DownNode();
          for (iter_.Dive(); !(iter_.Current() == ed); iter_.Next()) {
              queue.push_back(Coordinates(iter_.Current()));
          }
        }
        rest -= files;
        if (rest < 0 && h > 0) {
          std::cout << "Warning : Compact too much files(" << rest << ")." << std::endl;
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
  void OldPrintSimple(std::ostream& os) const {
    auto iter = NewIterator();
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
  void PrintSimple(std::ostream& os) const {
    auto iter = NewIterator();
    std::vector<std::vector<size_t>> map;
    size_t maxh = 0;
    os << "----------Print Simple Begin----------" << std::endl;
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next()) {
      auto height = iter->Current().node_->Height();
      std::vector<size_t> height_state;
      for (size_t i = 0; i < height; ++i)
        height_state.push_back(iter->Current().node_->LevelAt(i)->buffer_.size());
      map.push_back(height_state);
      if (height > maxh) maxh = height;
    }
    for (int h = maxh - 1; h >= 0; --h) {
      for (size_t i = 0; i < map.size(); ++i) {
        if (map[i].size() > h)
          os << (map[i][h] > 9 ? '@' : static_cast<const char>('0' + map[i][h]));
        else 
          os << ' ';
      }
      os << std::endl;
    }
    os << "----------Print Simple End----------" << std::endl;
  }
  void PrintStatistics(std::ostream& os) const {
    os << "----------Print Statistics Begin----------" << std::endl;
    Delineator d;
    auto iter = NewIterator();
    // return merged statistics.
    //for (iter->SeekToFirst(0); iter->Valid(); iter->Next())
    //  d.AddStatistics(iter->Current().node_->Guard(), iter->GetRouteMergedStatistics());
    // return only last level statistics.
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next())
      if (iter->Current().Buffer().size() == 1)
        d.AddStatistics(iter->Current().node_->Guard(), iter->Current().Buffer().GetStatistics());
    
    os << "----------Print KSGet----------" << std::endl;
    d.PrintTo(os, options_->NowTimeSlice(), KSGetCount);
    os << "----------Print KSPut----------" << std::endl;
    d.PrintTo(os, options_->NowTimeSlice(), KSPutCount);
    os << "----------Print KSIterate----------" << std::endl;
    d.PrintTo(os, options_->NowTimeSlice(), KSIterateCount);
    os << "----------Print Statistics End----------" << std::endl;
  }
 public:
  std::string ToString() const {
    std::stringstream ss;
#if defined(WITH_BVERSION_DEBUG)
    PrintDetailed(ss);
    //PrintStatistics(ss);
#else
    PrintSimple(ss);
    //PrintStatistics(ss);
#endif
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
  std::vector<std::pair<size_t, SBSNode::ValuePtr>>& Recycler() { return iter_.Recycler(); }

  bool isDirty() const {
    auto iter = NewIterator();
    iter->SeekDirty();
    if (iter->Current().height_ == 0) 
      return 0;
    return 1;
  }
};

}