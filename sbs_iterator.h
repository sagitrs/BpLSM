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
  bool Fit(const Bounded& range) const { return node_->Fit(height_, range); }
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
    while (Current().Fit(range) && Current().height_ > 0) {
      SBSNode::SBSP st = Current().node_;
      SBSNode::SBSP ed = Current().Next();
      size_t height = Current().height_ - 1;
      bool dive = false;
      for (Coordinates c = Coordinates(st, height); c.node_ != ed; c.JumpNext()) 
        if (c.Fit(range)) {
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
    while (history_.size() > 1) history_.pop();
    while (route_.size() > 1) route_.pop();
  }
  // Assert: Already SeekRange().
  void GoToTarget() { history_ = route_; }
  // Jump to a node within the tree that meets the requirements 
  // and save all nodes on the path.
  void SeekRange(const Bounded& range) {
    ResetToRoot();
    while (Current().Fit(range) && Current().height_ > 0) {
      SBSNode::SBSP st = Current().node_;
      SBSNode::SBSP ed = Current().Next();
      size_t height = Current().height_ - 1;
      bool dive = false;
      for (Coordinates c = Coordinates(st, height); c.node_ != ed; c.JumpNext()) 
        if (c.Fit(range)) {
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
    GoToTarget();
    while (history_.size() > 1 && !Current().Contains(value)) {
      history_.pop();
    } 
    return !(history_.size() > 1) 
        || Current().Contains(value);
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
  void Add(const SBSOptions& options, SBSNode::ValuePtr range) {
    GoToTarget();
    Current().Add(options, range);
    while (history_.size() >= 1) {
      if (Current().TestState(options) <= 0 || (Current().height_ > 0 && Current().IsDirty()))
        break;
      Current().SplitNext(options);
      history_.pop();
    }
  }
 private:
 public:
  void GetRangesInNode(BoundedValueContainer& results, std::shared_ptr<Bounded> range = nullptr) {
    GoToTarget();
    Current().GetRanges(results, range);
  }
  // Get all the values on the path that are overlap with the given range.
  void GetRangesOnRoute(BoundedValueContainer& results, std::shared_ptr<Bounded> range = nullptr) {
    GoToTarget();
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
  void Del(const SBSOptions& options, SBSNode::ValuePtr range) {
    GoToTarget();
    Current().Del(range);
    while (history_.size() > 1) {
      Coordinates target = Current();
      history_.pop();

      bool shrink = Current().TestState(options) < 0;
      if (!shrink) break;

      SBSNode::SBSP st = Current().node_;
      SBSNode::SBSP ed = Current().Next();
      size_t h = target.height_;
      for (Coordinates i = Coordinates(st, h); i.node_ != ed; i.JumpNext())
        if (i.Next() != ed && i.node_ == target.node_) {
          size_t prev_width = i.node_->Width(h);
          SBSNode::SBSP next = target.Next();
          if (next == ed) next = nullptr;
          if (next && next->Width(h) < prev_width) {
            i.JumpNext();
            assert(i.node_ == target.node_);
          }
          i.AbsorbNext(options);
          if (i.TestState(options) > 0 && !i.IsDirty())
            i.SplitNext(options);
          break;
        }
    }
    // Since the path node may have changed, we always assume that 
    // the path information is no longer accessible and 
    // therefore return to the root node.
    ResetToRoot();
  }

  void TargetIncStatistics(Counter::TypeLabel label, int size) { 
    GoToTarget();
    Current().IncStatistics(label, size); 
  }
  std::string ToString() {
    GoToTarget();
    std::stringstream ss;
    while (!history_.empty()) {
      ss << Current().ToString() << ",";
      history_.pop();
    }
    GoToTarget();
    return ss.str();
  }
};


}  