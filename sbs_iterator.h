#pragma once

#include <stack>
#include "sbs_node.h"
#include "scorer.h"
namespace sagitrs {
  //---------------------------------Iterator-----------------------------
  // The data structure looks more like a syntactic sugar, 
  // which treats each layer of SBSNode as a separate "node".
struct Coordinates {
  SBSNode::SBSP node_;
  size_t height_;
  Coordinates(SBSNode::SBSP node, size_t height) 
  : node_(node), height_(height){}
  
  SBSNode::SBSP Next() const { return node_->Next(height_); }
  void JumpNext() { node_ = Next(); }
  int TestState(const SBSOptions& options) const { return node_->TestState(options, height_); }
  bool Fit(const Bounded& range, bool no_overlap) const { return node_->Fit(height_, range, no_overlap); }
  void Del(SBSNode::ValuePtr range) const { node_->Del(height_, range); }
  void Add(const SBSOptions& options, SBSNode::ValuePtr range) const { node_->Add(options, height_, range); }
  bool Contains(SBSNode::ValuePtr range) const { return node_->level_[height_]->Contains(range); }
  void SplitNext(const SBSOptions& options) { node_->SplitNext(options, height_); }
  void AbsorbNext(const SBSOptions& options) { node_->AbsorbNext(options, height_); }
  //bool operator ==(const Coordinates& b) { return node_ == b.node_; }
  bool IsDirty() const { return node_->level_[height_]->isDirty(); }
  void IncStatistics(Counter::TypeLabel label, int size) { node_->level_[height_]->node_stats_->Inc(label, size); }
  void GetRanges(BoundedValueContainer& results, std::shared_ptr<Bounded> key = nullptr) {
    auto& buffer = node_->level_[height_]->buffer_;
    for (auto i = buffer.begin(); i != buffer.end(); ++i) {
      if (key == nullptr || (*i)->Compare(*key) == BOverlap)
        results.Add(*i);
    }
  }
  BoundedValueContainer& Buffer() { return node_->level_[height_]->buffer_; }

  std::string ToString() const {
    std::string node = node_->Guard().ToString();
    std::string height = std::to_string(height_);
    return "(" + node + "," + height + ")";
  }
  //bool operator==(const Coordinates& coor) const = delete;
  bool operator==(const Coordinates& coor) const {
    return node_ == coor.node_ && height_ == coor.height_;
  }
};

struct SBSIterator {
  private:
  //TypeScorer scorer_;
  std::stack<Coordinates> history_, route_;
  Coordinates& Current() { return history_.top(); }
  void Push(const Coordinates& coor) {
    history_.push(coor);
    route_.push(coor);
  }
  bool SeekNode(Coordinates target) {
    ResetToRoot();
    BRealBounded range(target.node_->Guard(), target.node_->Guard());
    while (Current().Fit(range, false) && Current().height_ > 0) {
      SBSNode::SBSP st = Current().node_;
      SBSNode::SBSP ed = Current().Next();
      size_t height = Current().height_ - 1;
      bool dive = false;
      for (Coordinates c = Coordinates(st, height); c.node_ != ed; c.JumpNext()) 
        if (c.Fit(range, false)) {
          Push(c);
          dive = true;
          if (c == target) return 1;
          break;
        }
      if (!dive) break;
    }
    if (Current().height_ == 0 && Current() == target)
      return 1;
    return 0;
  }
  public:
  SBSIterator(SBSNode::SBSP head, int height = -1) 
  : //scorer_(),
    history_() {
      //scorer_.SetGlobalStatus(head->Height());
      Push(Coordinates(head, height < 0 ? head->Height()-1 : height));
  }
  void ResetToRoot() { 
    while (route_.size() > 1) route_.pop();
    LoadRoute();
  }
  // Assert: Already SeekRange().
  void LoadRoute() { history_ = route_; }
  void StoreRoute() { route_ = history_; }
  // Jump to a node within the tree that meets the requirements 
  // and save all nodes on the path.
  void SeekRange(const Bounded& range, bool no_level0_overlap = false) {
    ResetToRoot();
    while (Current().Fit(range, false) && Current().height_ > 0) {
      SBSNode::SBSP st = Current().node_;
      SBSNode::SBSP ed = Current().Next();
      size_t height = Current().height_ - 1;
      bool dive = false;
      for (Coordinates c = Coordinates(st, height); c.node_ != ed; c.JumpNext()) 
        if (c.Fit(range, c.height_ == 0 && no_level0_overlap)) {
          Push(c);
          dive = true;
          break;
        }
      if (!dive) break;
    }
  }
  // Find elements on the path that exactly match the target object 
  // (including ranges and values).
  // Assert: Already SeekRange().
  bool SeekValue(SBSNode::ValuePtr value) {
    LoadRoute();
    while (!history_.empty()) {
      bool found = Current().Contains(value);
      if (found) {
        StoreRoute();
        return 1;
      }
      history_.pop();
    } 
    return 0;
  }
  // Add a value to the current node.
  // This may recursively trigger a split operation.
  // Assert: Already SeekRange().
  void SeekScore(std::shared_ptr<Scorer> scorer) {
    ResetToRoot();
    size_t max_height = Current().height_;
    double max_score = 0;
    Coordinates target(Current());
    for (int height = max_height; height >= 0; --height) {
      for (auto node = Current().node_; node != nullptr; node = node->Next(height)) {
        double s = scorer->Calculate(node, height);
        if (s > max_score) {
          max_score = s;
          target = Coordinates(node, height);
          if (s == 1) break;
        }
      }
      if (max_score == 1) break;
    }
    bool ok = SeekNode(target);
    assert(ok);
  }
 private:
  void CheckSplit(const SBSOptions& options) {
    while (history_.size() >= 1) {
      while (Current().TestState(options) > 0) {
        // Dirty node can not split.
        if (Current().height_ > 0 && Current().IsDirty()) 
          break;
        Current().SplitNext(options);
      }
      history_.pop();
    }
  }
 public:
  void Add(const SBSOptions& options, SBSNode::ValuePtr range) {
    SeekRange(*range);
    LoadRoute();
    Current().Add(options, range);
    CheckSplit(options);
  }
 private:
 public:
  void GetRangesInNode(BoundedValueContainer& results, std::shared_ptr<Bounded> range = nullptr) {
    LoadRoute();
    Current().GetRanges(results, range);
  }
  // Get all the values on the path that are overlap with the given range.
  void GetRangesOnRoute(BoundedValueContainer& results, std::shared_ptr<Bounded> range = nullptr) {
    LoadRoute();
    while (!history_.empty()) {
      Current().GetRanges(results, range);
      history_.pop();
    }
  }
  // Assume: The current node contains the target value.
  // Delete a value within the current node which is the same as the given value.
  // This may recursively trigger a merge operation and possibly a split operation.
  // Assert: Already SeekRange().
  // Assert: Already SeekValue().
  bool Del(const SBSOptions& options, SBSNode::ValuePtr range) {
    std::stack<SBSNode::ValuePtr> s;
    bool ok = Del_(options, range, s);
    if (!ok) return 0;
    //s.push(range);
    while (!s.empty()) {
      auto e = s.top();
      s.pop();
      bool ok = Del_(options, e, s);
      assert(ok);
      Add(options, e);
    }
    return 1;
  }
  bool Del_(const SBSOptions& options, SBSNode::ValuePtr range, std::stack<SBSNode::ValuePtr>& stack) {
    SeekRange(*range);
    bool contains = SeekValue(range);
    if (!contains) return 0;
    LoadRoute();
    Current().Del(range);
    // Node not deleted, not cleared.
    if (Current().IsDirty()) return 1;
    if (Current().TestState(options) > 0) {
      // delayed split.
      CheckSplit(options);
      return 1;
    } else {
      Coordinates target = Current();
      bool shrink = Current().TestState(options) < 0;
      while (history_.size() > 1) {
        bool shrink = Current().TestState(options) < 0;
        if (!shrink) break;

        Coordinates target = Current();
        history_.pop();
        
        SBSNode::SBSP st = Current().node_;
        SBSNode::SBSP ed = Current().Next();
        size_t h = target.height_;
        SBSNode::SBSP prev = nullptr;
        SBSNode::SBSP next = target.Next() == ed ? nullptr : target.Next();
        if (target.node_ != st) { 
          for (Coordinates i = Coordinates(st, h); i.node_ != ed; i.JumpNext())
            if (i.Next() == target.node_) {
              prev = i.node_;
              break;
            }
          assert(prev != nullptr);
        }
        if (prev && (!next || prev->Width(h) < next->Width(h)))
          target.node_ = prev;
        else
          assert(next != nullptr);
        target.AbsorbNext(options);
        // Check if this node should split.
        if (target.TestState(options) > 0 && !target.IsDirty()) {
          target.SplitNext(options);
        }
        // Check if files in buffer can be placed in under level.
        for (auto element : Current().Buffer()) {
          for (auto i = Coordinates(st, h); i.node_ != ed; i.JumpNext()) {
            if (i.Fit(*element, h == 0)) {
              stack.push(element);
              break;
            }
          }
        }
      }
      ResetToRoot();
      return 1;
    }
    // Since the path node may have changed, we always assume that 
    // the path information is no longer accessible and 
    // therefore return to the root node.
  }

  void TargetIncStatistics(Counter::TypeLabel label, int size) { 
    LoadRoute();
    Current().IncStatistics(label, size); 
  }
  std::string ToString() {
    LoadRoute();
    std::stringstream ss;
    while (!history_.empty()) {
      ss << Current().ToString() << ",";
      history_.pop();
    }
    LoadRoute();
    return ss.str();
  }
};


}  