#pragma once

#include <stack>
#include "sbs_node.h"
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
  bool operator ==(const Coordinates& b) { return node_ == b.node_; }
  bool IsDirty() const { return node_->level_[height_]->isDirty(); }
};

struct SBSIterator {
  private:
  //TypeScorer scorer_;
  std::stack<Coordinates> history_;
  Coordinates& Current() { return history_.top(); }
  public:
  SBSIterator(SBSNode::SBSP head, int height = -1) 
  : //scorer_(),
    history_() {
      //scorer_.SetGlobalStatus(head->Height());
      history_.emplace(head, height < 0 ? head->Height()-1 : height);
  }

  void ReturnToRoot() { while (history_.size() > 1) history_.pop(); }
  // Jump to a node within the tree that meets the requirements 
  // and save all nodes on the path.
  void TraceRoute(const Bounded& range) {
    while (Current().Fit(range) && Current().height_ > 0) {
      SBSNode::SBSP st = Current().node_;
      SBSNode::SBSP ed = Current().Next();
      size_t height = Current().height_ - 1;
      bool dive = false;
      for (Coordinates c = Coordinates(st, height); c.node_ != ed; c.JumpNext()) 
        if (c.Fit(range)) {
          history_.push(c);
          dive = true;
          break;
        }
      if (!dive) break;
    }
  }
  // Find elements on the path that exactly match the target object 
  // (including ranges and values).
  bool Seek(SBSNode::ValuePtr value) {
    while (history_.size() > 1 && !Current().Contains(value)) {
      history_.pop();
    } 
    return !(history_.size() > 1) 
        || Current().Contains(value);
  }
  // Add a value to the current node.
  // This may recursively trigger a split operation.
  void Add(const SBSOptions& options, SBSNode::ValuePtr range) {
    Current().Add(options, range);
    while (history_.size() >= 1) {
      if (Current().TestState(options) <= 0 || (Current().height_ > 0 && Current().IsDirty()))
        break;
      Current().SplitNext(options);
      history_.pop();
    }
    ReturnToRoot();
  }
  // Assume: The current node contains the target value.
  // Delete a value within the current node which is the same as the given value.
  // This may recursively trigger a merge operation and possibly a split operation.
  void Del(const SBSOptions& options, SBSNode::ValuePtr range) {
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
        if (i.Next() != ed && i == target) {
          size_t prev_width = i.node_->Width(h);
          SBSNode::SBSP next = target.Next();
          if (next == ed) next = nullptr;
          if (next && next->Width(h) < prev_width) {
            i.JumpNext();
            assert(i == target);
          }
          i.AbsorbNext(options);
          if (i.TestState(options) > 0 && !i.IsDirty())
            i.SplitNext(options);
          break;
        }
    }
    ReturnToRoot();
  }
};


}  