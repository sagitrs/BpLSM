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
  : node_(node), height_(height) {}
  
  SBSNode::SBSP Next() const { return node_->Next(height_); }
  void JumpNext() { node_ = Next(); }
  void JumpDown(size_t down = 1) { assert(height_ >= down); height_ -= down; }
  bool Valid() const { return node_ != nullptr; }

  Coordinates NextNode() const { return Coordinates(Next(), height_); }
  Coordinates DownNode() const { assert(height_ > 0); return Coordinates(node_, height_ - 1); }

  int TestState(const SBSOptions& options) const { return node_->TestState(options, height_); }
  bool Fit(const Bounded& range, bool no_overlap) const { return node_->Fit(height_, range, no_overlap); }
  void Del(SBSNode::ValuePtr range) const { node_->Del(height_, range); }
  void Add(const SBSOptions& options, SBSNode::ValuePtr range) const { node_->Add(options, height_, range); }
  bool Contains(SBSNode::ValuePtr value) const { return node_->level_[height_]->Contains(value); }
  void SplitNext(const SBSOptions& options) { node_->SplitNext(options, height_); }
  void AbsorbNext(const SBSOptions& options) { node_->AbsorbNext(options, height_); }
  void RefreshChildStatistics() { node_->RefreshChildStatistics(height_); }
  //bool operator ==(const Coordinates& b) { return node_ == b.node_; }
  bool IsDirty() const { return node_->level_[height_]->isDirty(); }
  void GetRanges(BoundedValueContainer& results, std::shared_ptr<Bounded> key = nullptr) {
    auto& buffer = node_->level_[height_]->buffer_;
    auto& statistics = node_->level_[height_]->node_stats_;
    for (auto i = buffer.begin(); i != buffer.end(); ++i) {
      if (key == nullptr || (*i)->Compare(*key) == BOverlap) {
        results.Add(*i);
        Slice guard = (*i)->Min();
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
  size_t Size() const { return std::vector<Coordinates>::size(); }
  bool Empty() const { return empty(); }

  Coordinates& Top() { return *rbegin(); }
  Coordinates& Bottom(){ return *begin(); }
  const Coordinates& Top() const { return *rbegin(); }
  const Coordinates& Bottom() const { return *begin(); }
  Coordinates& operator[](size_t k) { return std::vector<Coordinates>::operator[](k); }
  const Coordinates& operator[](size_t k) const { return (*this)[k]; }
  Coordinates& reverse_at(size_t k) { return (*this)[size() - 1 - k]; }

  void Push(const Coordinates& coor) { push_back(coor); }
  Coordinates Pop() {
    assert(!empty());
    Coordinates result = *rbegin();
    pop_back();
    return result;
  }
  
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

  void Resize(size_t k) {
    assert(k <= size());
    erase(begin() + k, end());
  }
  void Clear() { clear(); }
  void SetToIterator(const CoordinatesStackIterator& iter) { Resize(iter.CurrentCursor() + 1); }
  std::shared_ptr<CoordinatesStackIterator> NewIterator() { 
    return std::make_shared<CoordinatesStackIterator>(this); 
  }
};

struct SBSIterator {
 private:
  CoordinatesStack s_;
  SBSNode::SBSP head_;
  //-------------------------------------------------------------
  size_t SBSHeight() const { assert(Valid()); return head_->Height(); }
  //----------------------iterator operation---------------------
 public:
  bool Valid() const { return s_.Top().Valid(); }
  void SeekToRoot() { 
    s_.Clear();
    s_.Push(Coordinates(head_, head_->Height()-1)); 
  }
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
  Coordinates Current() const { return s_.Top(); }
  // ---------------------iterator operation end-----------------
 private:
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
  SBSIterator(SBSNode::SBSP head) : s_(), head_(head) { SeekToRoot(); }
  // Jump to a node within the tree that meets the requirements 
  // and save all nodes on the path.
  void SeekRange(const Bounded& range, bool no_level0_overlap = false) {
    SeekToRoot();
    assert(s_.Top().height_ > 0);
    assert(s_.Top().Fit(range, false));

    while (s_.Top().height_ > 0) {
      auto stop = s_.Top().NextNode().DownNode();
      bool dive = false;
      for (Coordinates c = s_.Top().DownNode(); c.Valid() && !(c == stop); c.JumpNext()) 
        if (c.Fit(range, c.height_ == 0 && no_level0_overlap)) {
          s_.Push(c);
          dive = true;
          break;
        }
      if (!dive) break;
    }
  }
  bool SeekBoundedValue(SBSNode::ValuePtr value) {
    SeekToRoot();
    if (s_.Top().Contains(value)) return 1;
    while (s_.Top().Fit(*value, false) && s_.Top().height_ > 0) {
      auto ed = s_.Top().NextNode().DownNode();
      bool dive = false;
      for (Coordinates c = s_.Top().DownNode(); c.Valid() && !(c == ed); c.JumpNext()) 
        if (c.Fit(*value, false)) {
          s_.Push(c);
          if (s_.Top().Contains(value))
            return 1;
          dive = true;
          break;
        }
      if (!dive) break;
    }
    return 0;
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
    SeekToRoot();
    CoordinatesStack max_stack(s_);
    double max_score = scorer->Calculate(s_.Bottom().node_, s_.Bottom().height_);
    for (int height = SBSHeight() - 1; height > 0; --height) {
      SeekToRoot();
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
    auto iter = s_.NewIterator();
    bool update = false;
    for (iter->SeekToLast(); iter->Valid() && iter->Current().TestState(options) > 0; iter->Prev()) {
      // Dirty node can not split.
      if (iter->Current().height_ > 0 && iter->Current().IsDirty()) 
        break;
      update = true;
      do {
        iter->Current().SplitNext(options);
      } while (iter->Current().TestState(options) > 0);
    }
    // --------
    if (update)
      for (; iter->Valid(); iter->Prev())
        iter->Current().RefreshChildStatistics();
  }
 public:
  void Add(const SBSOptions& options, SBSNode::ValuePtr range) {
    SeekRange(*range, true);
    s_.Top().Add(options, range);
    CheckSplit(options);
  }
  void AddBuffered(const SBSOptions& options, SBSNode::ValuePtr range) { s_.Bottom().Add(options, range); }
  void GetBuffered(BoundedValueContainer& results) { s_.Bottom().GetRanges(results); }
 private:
 public:
  // Get all the values on the path that are overlap with the given range.
  void GetBufferOnRoute(BoundedValueContainer& results, std::shared_ptr<Bounded> range = nullptr) {
    auto iter = s_.NewIterator();
    for (iter->SeekToFirst(); iter->Valid(); iter->Next())
      iter->Current().GetRanges(results, range);
  }
  void GetBufferInCurrent(BoundedValueContainer& results, std::shared_ptr<Bounded> range = nullptr) {
    s_.Top().GetRanges(results, range);
  }
  void GetChildGuardInCurrent(BoundedValueContainer& results) {
    if (s_.Top().height_ == 0) return;
    auto ed = s_.Top(); 
    ed.JumpNext(); 
    ed.JumpDown();
    for (auto i = Coordinates(s_.Top().node_, ed.height_); i.Valid() && !(i == ed); i.JumpNext()) {
      auto cp = i.node_->pacesetter_;
      if (cp != nullptr)
        results.push_back(cp);
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
  bool Del_(const SBSOptions& options, SBSNode::ValuePtr value, std::stack<SBSNode::ValuePtr>& stack) {
    bool found = SeekBoundedValue(value);
    if (!found) return 0;
    bool update = false;
    s_.Top().Del(value);
    int state = s_.Top().TestState(options);
    if (state > 0) {
      // split delayed.
      // leave absorb procedure.
      if (!s_.Top().IsDirty())
        CheckSplit(options);
      return 1;
    }
    while (s_.Size() > 1) {
      auto target = s_.Pop();
      size_t height = target.height_;
      auto st = s_.Top().DownNode(), ed = s_.Top().NextNode().DownNode();
      if (state < 0) {
        Coordinates prev = st;
        auto next = target.NextNode();
        if (!(target == st)) { 
          for (Coordinates i = st; i.Valid() && !(i == ed); i.JumpNext())
            if (i.NextNode() == target) {
              prev = i;
              break;
            }
        }
        // init finished.
        if (target == st) { // no prev node.
          assert(!(next == ed));
        } else if (!(next == ed) && prev.node_->Width(height) > next.node_->Width(height)) { // prev.width > next.width
          ;
        } else {
          assert(next == ed || prev.node_->Width(height) <= next.node_->Width(height));
          target = prev;
        }
        target.AbsorbNext(options);
        // Check split by absorb.
        if (target.TestState(options) > 0 && !target.IsDirty())
          target.SplitNext(options);
      }
      // deletion finished. Check file bound.
      for (auto element : s_.Top().Buffer()) {
        for (auto i = st; i.Valid() && !(i == ed); i.JumpNext()) {
          if (i.Fit(*element, height == 0)) {
            stack.push(element);
            break;
          }
        }
      }
      state = s_.Top().TestState(options);
    }
    auto iter = s_.NewIterator();
    for (iter->SeekToLast(); iter->Valid(); iter->Prev())
      iter->Current().RefreshChildStatistics();
    
    // Since the path node may have changed, we always assume that 
    // the path information is no longer accessible and 
    // therefore return to the root node.
    SeekToRoot();
    return 1;
  }

  std::string ToString() {
    std::stringstream ss;
    auto iter = s_.NewIterator();
    for (iter->SeekToFirst(); iter->Valid(); iter->Next())
      ss << iter->Current().ToString() << ",";
    return ss.str();
  }
};


}  