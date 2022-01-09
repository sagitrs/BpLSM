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
  void JumpDown(size_t down = 1) { assert(height_ >= down); height_ -= down; }
  bool Valid() const { return node_ != nullptr; }

  int TestState(const SBSOptions& options) const { return node_->TestState(options, height_); }
  bool Fit(const Bounded& range, bool no_overlap) const { return node_->Fit(height_, range, no_overlap); }
  void Del(SBSNode::ValuePtr range) const { node_->Del(height_, range); }
  void Add(const SBSOptions& options, SBSNode::ValuePtr range) const { node_->Add(options, height_, range); }
  bool Contains(SBSNode::ValuePtr range) const { return node_->level_[height_]->Contains(range); }
  void SplitNext(const SBSOptions& options) { node_->SplitNext(options, height_); }
  void AbsorbNext(const SBSOptions& options) { node_->AbsorbNext(options, height_); }
  //bool operator ==(const Coordinates& b) { return node_ == b.node_; }
  bool IsDirty() const { return node_->level_[height_]->isDirty(); }
  void IncStatistics(const Slice& key, Counter::TypeLabel label, int size) { 
    node_->level_[height_]->node_stats_->Inc(key, label, size); 
  }
  void GetRanges(BoundedValueContainer& results, std::shared_ptr<Bounded> key = nullptr) {
    auto& buffer = node_->level_[height_]->buffer_;
    auto& statistics = node_->level_[height_]->node_stats_;
    for (auto i = buffer.begin(); i != buffer.end(); ++i) {
      if (key == nullptr || (*i)->Compare(*key) == BOverlap) {
        results.Add(*i);
        Slice guard = (*i)->Min();
        statistics->Inc(guard, DefaultCounterType::GetCount, 1);
      }
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

struct CoordinatesStack : private std::vector<Coordinates> {
 public: 
  size_t Size() const { return size(); }
  bool Empty() const { return empty(); }
  void Push(const Coordinates& coor) { push_back(coor); }
  Coordinates& Top() { return *rbegin(); }
  Coordinates& Bottom(){ return *begin(); }
  const Coordinates& Top() const { return *rbegin(); }
  const Coordinates& Bottom() const { return *begin(); }
  Coordinates Pop() {
    assert(!empty());
    Coordinates result = *rbegin();
    pop_back();
    return result;
  }
  void Resize(size_t k) { resize(k); }
  Coordinates& operator[](size_t k) { return (*this)[k]; }
  const Coordinates& operator[](size_t k) const { return (*this)[k]; }
  Coordinates& reverse_at(size_t k) { return (*this)[size() - 1 - k]; }
 public:// iterator related:
  struct CoordinatesStackIterator {
   private:
    CoordinatesStack *stack_;
    int curr_;
   public:
    CoordinatesStackIterator(CoordinatesStack* stack) : stack_(stack), curr_(-1) {}
    bool Valid() const { return (0 <= curr_) && (curr_ < stack_->size()); }
    void SeekToFirst() { curr_ = 0; }
    void SeekToLast() { curr_ = stack_->size() - 1; }
    void Prev() { curr_ --; }
    void Next() { curr_ ++; }
    
    Coordinates& Current() const { assert(Valid()); return (*stack_)[curr_]; }
    size_t CurrentCursor() const { assert(Valid()); return curr_; }
  };

  void SetToIterator(const CoordinatesStackIterator& iter) { resize(iter.CurrentCursor() + 1); }
  std::shared_ptr<CoordinatesStackIterator> NewIterator() { 
    return std::make_shared<CoordinatesStackIterator>(this); 
  }
};

struct SBSIterator {
 private:
  CoordinatesStack s_;
  //-------------------------------------------------------------
  size_t SBSHeight() const { assert(Valid()); return s_.Bottom().height_; }
  //----------------------iterator operation---------------------
  bool Valid() const { 
    return 1 <= s_.Size() 
        && s_.Size() <= s_.Bottom().height_
        && s_.Top().Valid(); 
  }
  void SeekToRoot() { s_.Resize(1); }
  void Next(size_t recursive = 1) {
    for (size_t i = 1; i <= recursive; ++i) {
      auto& curr = s_.Top();
      curr.JumpNext();
      if (!curr.Valid()) return;
      
      size_t height = curr.node_->Height();
      size_t depth = s_.Size();
      for (size_t h = 1; h < height; ++h) 
        s_.reverse_at(h) = Coordinates(curr.node_, h);
    }
  }
  virtual void Prev() = delete;
  void Dive(size_t recursive = 1) {
    auto& curr = s_.Top();
    assert(curr.height_ > 0);
    for (size_t i = 1; i <= recursive; ++i)
      s_.Push(Coordinates(curr.node_, curr.height_ - i));
  }
  // ---------------------iterator operation---------------------
  bool SeekNode(Coordinates target) {
    BRealBounded bound(target.node_->Guard(), target.node_->Guard());
    SeekRange(bound, false);
    auto iter = s_.NewIterator();
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
      if (iter->Current() == target) 
        return 1;
    }
    return 0;
  }
  public:
  SBSIterator(SBSNode::SBSP head, int height = -1) : s_() {
    s_.Push(Coordinates(head, height < 0 ? head->Height()-1 : height));
  }
  // Jump to a node within the tree that meets the requirements 
  // and save all nodes on the path.
  void SeekRange(const Bounded& range, bool no_level0_overlap = false) {
    s_.Resize(1);
    while (s_.Top().Fit(range, false) && s_.Top().height_ > 0) {
      size_t height = s_.Top().height_ - 1;
      auto st = Coordinates(s_.Top().node_, height);
      auto ed = Coordinates(s_.Top().Next(), height);
      bool dive = false;
      for (Coordinates c = st; !(c == ed); c.JumpNext()) 
        if (c.Fit(range, c.height_ == 0 && no_level0_overlap)) {
          s_.Push(c);
          dive = true;
          break;
        }
      if (!dive) break;
    }
  }
  // Find elements on the path that exactly match the target object 
  // (including ranges and values).
  // Assert: Already SeekRange().
  bool RouteContainsValue(SBSNode::ValuePtr value) {
    auto iter = s_.NewIterator();
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
      if (iter->Current().Contains(value))
        return 1;
    }
    return 0;
  }
  // Add a value to the current node.
  // This may recursively trigger a split operation.
  // Assert: Already SeekRange().
  double SeekScore(std::shared_ptr<Scorer> scorer) {
    s_.Resize(1);
    CoordinatesStack max_stack(s_);
    double max_score = scorer->Calculate(s_.Bottom().node_, s_.Bottom().height_);
    for (int height = SBSHeight() - 1; height >= 0; --height) {
      s_.Resize(1);
      for (Dive(SBSHeight() - 1 - height); Valid(); Next()) {
        double s = scorer->Calculate(s_.Top().node_, height);
        if (s > max_score) {
          max_score = s;
          max_stack = s_;
          if (s == 1) break;
        }
      }
      if (max_score == 1) break;
    }
    s_ = max_stack;
    return max_score;
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
    SeekRange(*range, true);
    LoadRoute();
    Current().Add(options, range);
    CheckSplit(options);
  }
  void AddBuffered(const SBSOptions& options, SBSNode::ValuePtr range) {
    ResetToRoot();
    Current().Add(options, range);
  }
  void GetBuffered(BoundedValueContainer& results) {
    ResetToRoot();
    Current().GetRanges(results);
  }
 private:
 public:
  void GetRangesInCurrent(BoundedValueContainer& results, std::shared_ptr<Bounded> range = nullptr) {
    Current().GetRanges(results, range);
  }
  void GetChildGuardInCurrent(BoundedValueContainer& results) {
    if (Current().height_ == 0) return;
    auto ed = Current();
    ed.JumpNext();
    auto st = Current();
    for (st.JumpDown(); st.node_ != ed.node_; st.JumpNext()) {
      auto cp = st.node_->pacesetter_;
      if (cp != nullptr)
        results.push_back(cp);
    }
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
      while (history_.size() > 1) {
        bool shrink = Current().TestState(options) < 0;
        Coordinates target = Current();
        history_.pop();
        SBSNode::SBSP st = Current().node_;
        SBSNode::SBSP ed = Current().Next();
        size_t h = target.height_;
        if (shrink) {
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
        if (!shrink) break;
      }
      ResetToRoot();
      return 1;
    }
    // Since the path node may have changed, we always assume that 
    // the path information is no longer accessible and 
    // therefore return to the root node.
  }

  void IncStatistics(const Slice& key, Counter::TypeLabel label, int size) {
    Current().IncStatistics(key, label, size);
  }
  void TargetIncStatistics(const Slice& key, Counter::TypeLabel label, int size) { 
    LoadRoute();
    IncStatistics(key, label, size);
  }

  void JumpDown(int down = 1) {
    auto node = Current();
    node.JumpDown(down);
    history_.push(node);
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
  bool Valid() const { return Current().node_ != nullptr; }
  size_t BufferSize() const { return Current().Buffer().size(); }
  size_t Height() const { return Current().height_; }
  Slice Guard() const { return Current().node_->Guard(); }
  std::shared_ptr<BoundedValue> Pacesetter() const { return Current().node_->pacesetter_; }
};


}  