#pragma once

#include <stack>
#include <unordered_set>
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
  size_t Width() const { return node_->Width(height_); }
  bool Fit(const Bounded& range, bool no_overlap) const { return node_->Fit(height_, range, no_overlap); }
  SBSNode::ValuePtr Del(SBSNode::ValuePtr range) const { return node_->Del(height_, range); }
  void Add(const SBSOptions& options, SBSNode::ValuePtr range) const { node_->Add(options, height_, range); }
  bool Contains(SBSNode::ValuePtr value) const { return node_->level_[height_]->Contains(value); }
  void SplitNext(const SBSOptions& options) { node_->SplitNext(options, height_); }
  void AbsorbNext(const SBSOptions& options) { node_->AbsorbNext(options, height_); }
  void GetBufferWithChildGuard(BoundedValueContainer* results, BoundedValueContainer* guards) {
    if (results)
      GetRanges(*results, nullptr); 
    if (height_ == 0) 
      return;
    if (guards)
      node_->GetChildGuard(height_, guards);
  }
  std::shared_ptr<Statistable> GetTreeStatistics() { return node_->GetTreeStatistics(height_); }
  std::shared_ptr<Statistable> GetNodeStatistics() { return node_->GetNodeStatistics(height_); }
  void SetStatisticsDirty() { node_->level_[height_]->statistics_dirty_ = true; }
  //bool operator ==(const Coordinates& b) { return node_ == b.node_; }
  bool IsDirty() const { return node_->level_[height_]->isDirty(); }
  void GetRanges(BoundedValueContainer& results, std::shared_ptr<Bounded> key = nullptr) {
    auto& buffer = node_->level_[height_]->buffer_;
    auto now = node_->options_->NowTimeSlice();
    for (auto i = buffer.begin(); i != buffer.end(); ++i) {
      if (key == nullptr || (*i)->Compare(*key) == BOverlap) {
        results.Add(*i);
        (*i)->UpdateStatistics(ValueGetCount, 1, now);
        SetStatisticsDirty();
        buffer.SetStatsDirty();
      }
    }
  }
  std::shared_ptr<BoundedValue> GetValue(uint64_t id) {
    auto& buffer = node_->level_[height_]->buffer_;
    for (auto i = buffer.begin(); i != buffer.end(); ++i)
      if ((*i)->Identifier() == id) 
        return *i;
    return nullptr;
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
  std::shared_ptr<CoordinatesStackIterator> NewIterator() const { 
    return std::make_shared<CoordinatesStackIterator>(const_cast<CoordinatesStack*>(this)); 
  }
};

struct SBSIterator : public Printable {
 private:
  CoordinatesStack s_;
  SBSNode::SBSP head_;
  std::vector<SBSNode::ValuePtr> recycler_, reinserter_;
  //std::deque<SBSNode::ValuePtr> reinserter_;
  //-------------------------------------------------------------
  size_t SBSHeight() const { return head_->Height(); }
  //----------------------iterator operation---------------------
 public:
  bool Valid() const { return s_.Top().Valid(); }
  void SeekToRoot() { 
    s_.Clear();
    s_.Push(Coordinates(head_, head_->Height()-1)); 
    //recycler_.clear();
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
    auto curr = s_.Top();
    assert(curr.height_ > 0);
    for (size_t i = 1; i <= recursive; ++i)
      s_.Push(Coordinates(curr.node_, curr.height_ - i));
  }
  Coordinates Current() const { return s_.Top(); }
  void SeekToFirst(int level) {
    SeekToRoot();
    assert(level <= SBSHeight() - 1);
    Dive(SBSHeight() - 1 - level);
  }
  // ---------------------iterator operation end-----------------
 public:
  bool SeekNode(Coordinates target) {
    RealBounded bound(target.node_->Guard(), target.node_->Guard());
    SeekRange(bound, false);
    auto iter = s_.NewIterator();
    for (iter->SeekToLast(); iter->Valid(); iter->Prev()) {
      if (iter->Current() == target) {
        s_.SetToIterator(*iter);
        return 1;
      }
    }
    SeekToRoot();
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
  // Find elements on the path that exactly match the target object 
  // (including ranges and values).
  // Assert: Already SeekRange().
  std::shared_ptr<BoundedValue> SeekValueInRoute(uint64_t id) {
    auto iter = s_.NewIterator();
    std::shared_ptr<BoundedValue> res = nullptr;
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
      res = iter->Current().GetValue(id);
      if (res) {
        s_.SetToIterator(*iter);
        return res;
      }
    }
    return nullptr;
  }
  // Add a value to the current node.
  // This may recursively trigger a split operation.
  // Assert: Already SeekRange().
  double SeekScore(std::shared_ptr<Scorer> scorer, double baseline, bool optimal) {
    scorer->Init(head_);
    scorer->Reset(baseline);
    CoordinatesStack max_stack(s_);
    
    //for (int height = 1; height < SBSHeight(); ++height) {
    for (int height = SBSHeight() - 1; height > 0; --height) {
      SeekToRoot();
      for (Dive(SBSHeight() - 1 - height); Valid(); Next()) 
        if (scorer->Update(s_.Top().node_, s_.Top().height_)) { 
          max_stack = s_;
          if (!optimal)
            return scorer->MaxScore();
        }
    }
    s_ = max_stack;
    return scorer->MaxScore();
  }
 private:
  void CheckSplit(const SBSOptions& options) {
    auto iter = s_.NewIterator();
    bool update = false;
    for (iter->SeekToLast(); iter->Valid() && iter->Current().TestState(options) > 0; iter->Prev()) {
      // Dirty node can not split.
      if (iter->Current().height_ > 0 && iter->Current().IsDirty()) {
        break;
      }
      update = true;
      do {
        iter->Current().SplitNext(options);
      } while (iter->Current().TestState(options) > 0);
    }
  }
 public:
  void SetRouteStatisticsDirty() {
    auto iter = s_.NewIterator();
    iter->SeekToLast();
    assert(iter->Valid());
    iter->Current().Buffer().SetStatsDirty();
    for (; iter->Valid(); iter->Prev()) 
      iter->Current().SetStatisticsDirty();
  }
  void Add(const SBSOptions& options, SBSNode::ValuePtr value) {
    SeekRange(*value, true);
    //UpdateTargetStatistics(value->Identifier(), DefaultTypeLabel::LeafCount, 1, options.NowTimeSlice());
    if (s_.Top().height_ == 0) 
      value->UpdateStatistics(DefaultTypeLabel::LeafCount, 1, options.NowTimeSlice());           // inc leaf.
    SetRouteStatisticsDirty();
    s_.Top().Add(options, value);
    //SetRouteStatisticsDirty();
    CheckSplit(options);
  }
  void AddBuffered(const SBSOptions& options, SBSNode::ValuePtr range) { s_.Bottom().Add(options, range); }
  void GetBuffered(BoundedValueContainer& results) { s_.Bottom().GetRanges(results); }
  std::vector<SBSNode::ValuePtr>& Recycler() { return recycler_; }
 private:
 public:
  // Get all the values on the path that are overlap with the given range.
  void GetBufferOnRoute(BoundedValueContainer& results, std::shared_ptr<Bounded> range) {
    auto iter = s_.NewIterator();
    for (iter->SeekToLast(); iter->Valid(); iter->Prev()) {
      iter->Current().GetRanges(results, range);
    }
  }
  void GetBufferInCurrent(BoundedValueContainer& results, std::shared_ptr<Bounded> range = nullptr) { s_.Top().GetRanges(results, range); }

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
  void GetBufferWithChildGuard(Coordinates target, BoundedValueContainer* results) const {
    //Coordinates target = s_.Top();
    target.GetBufferWithChildGuard(&results[0], &results[1]);
  }
  double GetScore(std::shared_ptr<Scorer> scorer) const { return scorer->GetScore(s_.Top().node_, s_.Top().height_); }
  // Assume: The current node contains the target value.
  // Delete a value within the current node which is the same as the given value.
  // This may recursively trigger a merge operation and possibly a split operation.
  void Reinsert(const SBSOptions& options) {
    //assert(reinserter_.empty());
    for (auto e : reinserter_) {
      bool ok = Del_(options, e, false);
      if (ok)
        Add(options, e);
    }
    reinserter_.clear();
  }
  bool Del(const SBSOptions& options, SBSNode::ValuePtr range, bool auto_reinsert = true) {
    bool ok = Del_(options, range);
    if (!ok) 
      return 0;
    if (auto_reinsert) 
      Reinsert(options);
    return 1;
  }
  bool Del_(const SBSOptions& options, SBSNode::ValuePtr value, bool recycle = true) {
    // 1. Delete file in target node.
    // 2. (for inner node) check if split happens. if so, stop here.
    // 3. (for leaf node) check if node became empty. if so, check absorb recursively.
    //   1. return to parent node, absorb child node.
    //   2. check if files in buffer could move down, record in reinsert.
    //   3. recalculate node status.
    //   4. check if child node is less enough to check absorb recursively.
    // 4. recalculate all nodes' child-state.
    SeekToRoot();
    SeekRange(*value);
    auto res0 = SeekValueInRoute(value->Identifier());
    if (res0 == nullptr) 
      return 0;
    //if (s_.Top().height_ == 0) s_.Top().->UpdateStatistics(DefaultTypeLabel::LeafCount, 1, options.NowTimeSlice());           // inc leaf.
    //SetRouteStatisticsDirty();

    auto target = s_.Top();
    auto res = target.Del(value);
    assert(res != nullptr);
    if (s_.Top().height_ == 0)
      res->UpdateStatistics(DefaultTypeLabel::LeafCount, -1, options.NowTimeSlice());         // statistics.
    SetRouteStatisticsDirty();
    if (recycle) recycler_.push_back(res);

    // for inner node:
    // check if recursively split is triggered.
    size_t height = target.height_;
    int state = target.TestState(options);
    if (height > 0 && state > 0 && !target.IsDirty()) {
      CheckSplit(options);
      return 1;
    }

    // for leaf node:
    // check if recursively absorb is triggered.
    for (target = s_.Pop(); s_.Size() > 1; target = s_.Pop()) {
      size_t height = target.height_;
      auto st = s_.Top().DownNode(), ed = s_.Top().NextNode().DownNode();

      if ( target.TestState(options) < 0) {
        // begin absorbing.
        Coordinates prev = st, next = target.NextNode();
        if (!(target == st)) { 
          for (Coordinates i = st; i.Valid() && !(i == ed); i.JumpNext())
            if (i.NextNode() == target) {
              prev = i;
              break;
            }
        }
        // prev & next is calculated.
        if (target == st) { // no prev node.
          assert(!(next == ed));
        } else if (!(next == ed) && prev.node_->Width(height) > next.node_->Width(height)) { // prev.width > next.width
          ;
        } else {
          assert(next == ed || prev.node_->Width(height) <= next.node_->Width(height));
          target = prev;
        }
        // now we need target node to absorb the next node.
        target.AbsorbNext(options);
      }

      // Check file bound since guard in this tree has changed.
      for (auto element : s_.Top().Buffer()) {
        for (auto i = st; i.Valid() && !(i == ed); i.JumpNext()) {
          if (i.Fit(*element, height == 0)) {
            reinserter_.push_back(element);
            break;
          }
        }
      }
    }
    SeekToRoot();
    return 1;
  }

  virtual void GetStringSnapshot(std::vector<KVPair>& snapshot) const override {
    auto iter = s_.NewIterator();
    for (iter->SeekToFirst(); iter->Valid(); iter->Next())
      snapshot.emplace_back("", iter->Current().ToString());
  }
  std::shared_ptr<Statistics> GetRouteMergedStatistics() {
    auto iter = s_.NewIterator();
    int div = 1;
    std::shared_ptr<Statistics> sum = nullptr;

    iter->SeekToLast();
    auto stats = iter->Current().Buffer().GetStatistics();
    if (stats)
      sum = std::make_shared<Statistics>(*stats);
    RealBounded bound = iter->Current().Buffer();

    for (iter->Prev(); iter->Valid(); iter->Prev()) {
      div *= iter->Current().Width();
      //auto stats = iter->Current().Buffer().GetStatistics();
      //if (stats == nullptr) continue;
      for (auto value : iter->Current().Buffer()) 
        if (bound.Compare(*value) == BOverlap) {
          auto s1 = std::dynamic_pointer_cast<Statistable>(value);
          auto stats = std::dynamic_pointer_cast<Statistics>(s1);
          if (sum == nullptr) 
            sum = std::make_shared<Statistics>(*stats);
          else {
            auto tmp = std::make_shared<Statistics>(*stats);
            tmp->ScaleStatistics(DefaultCounterTypeMax, 1, div);
            sum->MergeStatistics(tmp);
          }
      }
      //iter->Current().GetRanges(results, range);
    }
    return sum;
  }
};


}  