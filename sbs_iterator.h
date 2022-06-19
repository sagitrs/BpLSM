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

  inline Coordinates NextNode() const { return Coordinates(Next(), height_); }
  inline Coordinates DownNode() const { assert(height_ > 0); return Coordinates(node_, height_ - 1); }

  int TestState(const SBSOptions& options) const { return node_->TestState(options, height_); }
  size_t Width() const { return node_->Width(height_); }
  inline bool Fit(const Bounded& range, bool no_overlap) const { return node_->Fit(height_, range, no_overlap); }
  BFile* Del(const BFile& file) const { 
    return node_->Del(height_, file); 
  }
  void Add(const SBSOptions& options, SBSNode::ValuePtr range) const { node_->Add(options, height_, range); }
  bool Contains(const BFile& value) const { 
    return node_->GetLevel(height_)->Contains(value); 
  }
  bool SplitNext(const SBSOptions& options, BFileVec* force = nullptr) { 
    return node_->SplitNext(options, height_, force); 
  }
  void AbsorbNext(const SBSOptions& options) { node_->AbsorbNext(options, height_); }
  void GetBufferWithChildGuard(BFileVec* results, BFileVec* guards) {
    if (results)
      GetRanges(*results, nullptr); 
    if (height_ == 0) 
      return;
    if (guards)
      node_->GetChildGuard(height_, guards);
  }
  const Statistics* GetTreeStatistics() { 
    return node_->GetTreeStatistics(height_); }
  const Statistics* GetNodeStatistics() { 
    return node_->GetNodeStatistics(height_); }
  void SetStatisticsDirty() { node_->GetLevel(height_)->table_.SetDirty(); }
  //bool operator ==(const Coordinates& b) { return node_ == b.node_; }
  bool IsDirty() const { return node_->GetLevel(height_)->isDirty(); }
  void GetRanges(BFileVec& results, const Bounded* key = nullptr) {
    auto& buffer = node_->GetLevel(height_)->buffer_;
    auto now = node_->options_.NowTimeSlice();
    for (auto i = buffer.begin(); i != buffer.end(); ++i) {
      if (key == nullptr || (*i)->Compare(*key) == BOverlap) {
        results.Add(*i);
        (*i)->UpdateStatistics(ValueGetCount, 1, now);
        SetStatisticsDirty();
        buffer.SetStatsDirty();
      }
    }
  }
  void GetCovers(BFileVec& results, const Slice& key) const {
    auto& buffer = node_->GetLevel(height_)->buffer_;
    for (auto i = buffer.begin(); i != buffer.end(); ++i) {
      Slice min((*i)->Min()), max((*i)->Max());
      if (min.compare(key) <= 0 && key.compare(max) <= 0)
        results.Add(*i);
    }
  }
  BFile* GetValue(uint64_t id) {
    auto& buffer = node_->GetLevel(height_)->buffer_;
    for (auto i = buffer.begin(); i != buffer.end(); ++i)
      if ((*i)->Identifier() == id) 
        return *i;
    return nullptr;
  }
  BFileVec& Buffer() { return node_->GetLevel(height_)->buffer_; }
  BFile* GetHottest(int64_t time) { 
    return node_->GetHottest(height_, time); 
  }
  LevelNode::VariableTable& Table() { return node_->GetLevel(height_)->table_; }
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
  virtual ~CoordinatesStack() {}
  size_t Size() const { return std::vector<Coordinates>::size(); }
  bool Empty() const { return empty(); }

  Coordinates& Top() { return *rbegin(); }
  Coordinates& Bottom(){ return *begin(); }
  const Coordinates& Top() const { return *rbegin(); }
  const Coordinates& Bottom() const { return *begin(); }
  Coordinates& operator[](size_t k) { return std::vector<Coordinates>::operator[](k); }
  const Coordinates& operator[](size_t k) const { return std::vector<Coordinates>::operator[](k); }
  Coordinates& reverse_at(size_t k) { return (*this)[size() - 1 - k]; }

  inline void Push(const Coordinates& coor) { push_back(coor); }
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
  CoordinatesStackIterator* NewIterator() const {
    return new CoordinatesStackIterator(const_cast<CoordinatesStack*>(this));
  }
};

struct SBSIterator : public Printable {
 private:
  CoordinatesStack s_;
  SBSNode::SBSP head_;
  //std::vector<std::pair<size_t, SBSNode::ValuePtr>> recycler_;
  std::vector<SBSNode::ValuePtr> reinserter_;
  //std::deque<SBSNode::ValuePtr> reinserter_;
 public:
  //-------------------------------------------------------------
  size_t SBSHeight() const { return head_->Height(); }
  const CoordinatesStack& Stack() const { return s_; }
  //----------------------iterator operation---------------------
 public:
  bool Valid() const { return s_.Top().Valid(); }
  inline void SeekToRoot() { 
    s_.Clear();
    s_.Push(Coordinates(head_, head_->Height()-1)); 
    //recycler_.clear();
  }
  void ReplaceHead(SBSNode* new_head) {
    head_ = new_head;
    SeekToRoot();
  }
  void Next(size_t recursive = 1) {
    for (size_t i = 1; i <= recursive; ++i) {
      auto& curr = s_.Top();
      curr.JumpNext();
      if (!curr.Valid()) return;
      
      size_t h1 = curr.height_, h2 = curr.node_->Height();
      //size_t height = curr.node_->Height();
      //size_t depth = s_.Size();
      for (size_t h = h1; h < h2; ++h) 
        s_.reverse_at(h - h1) = Coordinates(curr.node_, h);
    }
  }
  virtual void Prev(size_t recursive = 1) { 
    Coordinates c(s_.Top());
    if (c.node_ == head_) {
      s_.Top().node_ = nullptr;
      return ;
    }
    while (s_.Top().node_ == c.node_) 
      s_.Pop();
    while (s_.Top().height_ > c.height_) {
      Dive();
      while(s_.Top().Next() != c.node_) 
        s_.Top().JumpNext();
    } 
    assert(s_.Top().height_ == c.height_);
  }
  void Dive(size_t recursive = 1) {
    auto curr = s_.Top();
    assert(curr.height_ > 0);
    for (size_t i = 1; i <= recursive; ++i)
      s_.Push(Coordinates(curr.node_, curr.height_ - i));
  }
  void Float(size_t recursive = 1) {
    for (size_t i = 0; i < recursive; ++i)
      s_.Pop();
  }
  Coordinates Current() const { return s_.Top(); }
  void SeekToFirst(int level) {
    SeekToRoot();
    assert(level <= SBSHeight() - 1);
    Dive(SBSHeight() - 1 - level);
  }
  void SeekToLast(int level) {
    SeekToRoot();
    assert(s_.Top().height_ > 0);

    while (s_.Top().height_ > 0) {
      auto c = s_.Top().DownNode();
      while (c.Next() != nullptr)
        c.JumpNext();
      s_.Push(c);
    }
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
        delete iter;
        return 1;
      }
    }
    delete iter;
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
  void SeekKeySpace(const Slice& key, bool only_seek_next = false) {
    if (only_seek_next) {
      assert(Current().height_ == 0);
      for (; Valid(); Next()) {
        BFile* a = Current().node_->GetLevel(0)->buffer_.GetOne();
        if (a && a->Min().compare(key) > 0) {
          std::cout << key.ToString() << "<" << a->Min().ToString() << std::endl;
          fflush(stdout);
          assert(!a || a->Min().compare(key) <= 0);
        }
        SBSNode* next = Current().Next();
        BFile* b = next ? next->GetLevel(0)->buffer_.GetOne() : nullptr;
        if (!b || key.compare(b->Min()) < 0) 
          return;
      }
    } else {
      RealBounded bound(key, key);
      SeekRange(bound);
      if (Current().height_ > 0) {
        SeekToFirst(0);
        //Dive(Current().height_);
      }
      SeekKeySpace(key, true);
    }
  }
  bool SeekCurrentPrev(std::vector<SBSNode*>& prev) {
    CoordinatesStack s2(s_);
    size_t size = s2.Bottom().node_->Height();
    SBSNode* node = Current().node_;
    assert(node != head_);
    size_t height = Current().node_->Height();
    
    while (s2.Top().height_ < height) {
      assert(s2.Top().node_ == node);
      s2.Pop();
    }
    assert(s2.Size() == size - height);
    for (int h = height-1; h >= 0; --h) {
      SBSNode* p = p = s2.Top().node_; 
      while (p && p->Next(h) != node) 
        p = p->Next(h);
      assert(p != nullptr);
      s2.Push(Coordinates(p, h));
    }
    assert(s2.Size() == size);

    prev.resize(size);
    for (size_t i = 0; i < size; ++i) 
      prev[s2[i].height_] = s2[i].node_;
    for (size_t i = 0; i < height; ++i) 
      assert(prev[i] && prev[i]->Next(i) == node);
      //.push_back(s2[i].node_);
    assert(prev.size() == size);
    return 1;
  }
  // Find elements on the path that exactly match the target object 
  // (including ranges and values).
  // Assert: Already SeekRange().
  BFile* SeekValueInRoute(uint64_t id) {
    auto iter = s_.NewIterator();
    BFile* res = nullptr;
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
      res = iter->Current().GetValue(id);
      if (res) {
        s_.SetToIterator(*iter);
        delete iter;
        return res;
      }
    }
    delete iter;
    return nullptr;
  }
  
  void SeekDirty() {
    for (int height = SBSHeight() - 1; height > 0; --height) {
      SeekToRoot();
      for (Dive(SBSHeight() - 1 - height); Valid(); Next()) 
        if (Current().Buffer().size() >= 1)
          return;
    }
  }
  // Add a value to the current node.
  // This may recursively trigger a split operation.
  // Assert: Already SeekRange().
  double SeekScore(Scorer& scorer, double baseline, bool optimal) {
    scorer.Reset(baseline);
    CoordinatesStack max_stack(s_);
    
    //for (int height = 1; height < SBSHeight(); ++height) {
    for (int height = SBSHeight() - 1; height > 0; --height) {
      SeekToRoot();
      for (Dive(SBSHeight() - 1 - height); Valid(); Next()) 
        if (Current().Buffer().size() > 0) {
          bool updated = scorer.Update(s_.Top().node_, s_.Top().height_);
          if (!updated) continue;
          max_stack = s_;
          if (optimal == false)
            return scorer.MaxScore();
        }
    }
    s_ = max_stack;
    return scorer.MaxScore();
  }
  double SeekScoreInHeight(int height, Scorer& scorer, double baseline, bool optimal) {
    scorer.Reset(baseline);
    CoordinatesStack max_stack(s_);
    
    for (SeekToFirst(height); Valid(); Next()) 
      if (scorer.Update(s_.Top().node_, s_.Top().height_)) { 
        max_stack = s_;
        if (!optimal)
          return scorer.MaxScore();
      }
    
    s_ = max_stack;
    return scorer.MaxScore();
  }
  bool CheckSplit(const SBSOptions& options) {
    auto iter = s_.NewIterator();
    bool update = false;
    for (iter->SeekToLast(); iter->Valid() && iter->Current().TestState(options) > 0; iter->Prev()) {
      while (iter->Current().TestState(options) > 0) {
        bool ok = iter->Current().SplitNext(options);
        // node is dirty.
        if (!ok) {
          SeekNode(iter->Current());
          return false;
        } 
      }
    }
    delete iter;
    return true;
  }
 public:
  void DisableRouteHottest() {
    assert(Current().height_ == 0);
    auto iter = s_.NewIterator();
    iter->SeekToLast();
    auto target = iter->Current().Buffer()[0];
    for (iter->Prev(); iter->Valid(); iter->Prev()) {
      auto &h = iter->Current().Table().hottest_;
      auto &t = iter->Current().Table().update_time_;
      if (h != nullptr && h->Identifier() == target->Identifier())
        h = nullptr;
      else
        break;
    }
    delete iter;
  }
  void SetRouteStatisticsDirty() {
    auto iter = s_.NewIterator();
    iter->SeekToLast();
    assert(iter->Valid());
    iter->Current().Buffer().SetStatsDirty();
    for (; iter->Valid(); iter->Prev()) 
      iter->Current().SetStatisticsDirty();
    delete iter;
  }
  bool Add(const SBSOptions& options, SBSNode::ValuePtr value) {
    SeekRange(*value, true);
    //UpdateTargetStatistics(value->Identifier(), DefaultTypeLabel::LeafCount, 1, options.NowTimeSlice());
    if (s_.Top().height_ == 0) 
      value->UpdateStatistics(DefaultTypeLabel::LeafCount, 1, options.NowTimeSlice());           // inc leaf.
    SetRouteStatisticsDirty();
    s_.Top().Add(options, value);
    //SetRouteStatisticsDirty();
    bool state = CheckSplit(options);
    return state;
  }
  //std::vector<std::pair<size_t, SBSNode::ValuePtr>>& Recycler() { return recycler_; }
 private:
 public:
  // Get all the values on the path that are overlap with the given range.
  void GetBufferOnRoute(BFileVec& results, const Slice& key) const {
    auto iter = s_.NewIterator();
    for (iter->SeekToLast(); iter->Valid(); iter->Prev())
      iter->Current().GetCovers(results, key);
    delete iter;
  }
  void GetBufferOnRoute(std::vector<BFile*>& results) const {
    auto iter = s_.NewIterator();
    for (iter->SeekToLast(); iter->Valid(); iter->Prev()) {
      BFileVec& buffer = iter->Current().Buffer();
      for (BFile* file : buffer) 
        results.push_back(file);
    }
    delete iter;
  }
  
  void GetBufferInCurrent(BFileVec& results) { 
    s_.Top().GetRanges(results); 
  }

  void GetChildGuardInCurrent(BFileVec& results) {
    if (s_.Top().height_ == 0) return;
    auto ed = s_.Top(); 
    ed.JumpNext(); 
    ed.JumpDown();
    for (auto i = Coordinates(s_.Top().node_, ed.height_); i.Valid() && !(i == ed); i.JumpNext()) {
      auto cp = i.node_->Pacesetter();
      if (cp != nullptr)
        results.push_back(cp);
    }
  }
  void GetBufferWithChildGuard(Coordinates target, BFileVec* results) const {
    //Coordinates target = s_.Top();
    target.GetBufferWithChildGuard(&results[0], &results[1]);
  }
  double GetScore(Scorer& scorer) const { 
    return scorer.GetScore(s_.Top().node_, s_.Top().height_); }
  // Assume: The current node contains the target value.
  // Delete a value within the current node which is the same as the given value.
  // This may recursively trigger a merge operation and possibly a split operation.
  void Reinsert(const SBSOptions& options) {
    //assert(reinserter_.empty());
    for (auto e : reinserter_) if (e != nullptr) {
      BFile* deleted = Del_(options, *e);
      //assert(deleted->DeletedLevel() > 0);
      if (deleted)
        Add(options, e);
      deleted->SetDeletedLevel(-1);
    }
    reinserter_.clear();
  }
  BFile* Del(const SBSOptions& options, const BFile& file, bool auto_reinsert = true) {
    BFile* deleted = Del_(options, file);
    int level = deleted->DeletedLevel();
    if (level == -1) 
      return nullptr;
    if (auto_reinsert || level == 0) 
      Reinsert(options);
    return deleted;
  }
  BFile* Del_(const SBSOptions& options, const BFile& file) {
    // 1. Delete file in target node.
    // 2. (for inner node) check if split happens. if so, stop here.
    // 3. (for leaf node) check if node became empty. if so, check absorb recursively.
    //   1. return to parent node, absorb child node.
    //   2. check if files in buffer could move down, record in reinsert.
    //   3. recalculate node status.
    //   4. check if child node is less enough to check absorb recursively.
    // 4. recalculate all nodes' child-state.
    SeekToRoot(); Slice a(file.Min()), b(file.Max());
    SeekRange(file);
    auto res0 = SeekValueInRoute(file.Identifier());
    if (res0 == nullptr) 
      return nullptr;
    //if (s_.Top().height_ == 0) s_.Top().->UpdateStatistics(DefaultTypeLabel::LeafCount, 1, options.NowTimeSlice());           // inc leaf.
    //SetRouteStatisticsDirty();

    auto target = s_.Top();
    BFile* res = target.Del(file);

    assert(res != nullptr);
    if (s_.Top().height_ == 0) {
      res->UpdateStatistics(DefaultTypeLabel::LeafCount, -1, options.NowTimeSlice());         // statistics.
      DisableRouteHottest();
    }
    SetRouteStatisticsDirty();
    //deleted = res.second;
    //if (recycle) recycler_.push_back(res);

    // for inner node:
    // check if recursively split is triggered.
    size_t height = target.height_;
    int state = target.TestState(options);
    if (height > 0 && state > 0 && !target.IsDirty()) {
      CheckSplit(options);
      return res;
    }

    // for leaf node:
    // check if recursively absorb is triggered.
    CheckAbsorb(options);

    return res;
  }

  void CheckAbsorb(const SBSOptions& options) {
    for (auto target = s_.Pop(); s_.Size() > 1; target = s_.Pop()) {
      size_t height = target.height_;
      auto st = s_.Top().DownNode(), ed = s_.Top().NextNode().DownNode();

      if (target.TestState(options) < 0) {
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
      for (BFile* file : s_.Top().Buffer()) 
       if (file->Type() == BFile::TypeHole) {
        for (auto i = st; i.Valid() && !(i == ed); i.JumpNext()) {
          if (i.Fit(*file, height == 0)) {
            reinserter_.push_back(file);
            break;
          }
        }
      }
    }
    SeekToRoot();
  }
  void CheckAbsorbOnlyNext(const SBSOptions& options) {
    CoordinatesStack s2(s_);
    for (auto target = s2.Pop(); s2.Size() > 1; target = s2.Pop()) {
      size_t height = target.height_;
      auto next = target.Next();
      
      bool need_absorb = target.TestState(options);
      bool could_absorb = next != nullptr && next->Height() == height + 1;
      if (!need_absorb || !could_absorb) return;
      {
        auto old0 = target.node_->GetLevel(height);
        auto old1 = next->GetLevel(height);
        auto newlnode = new LevelNode(*old1);
        newlnode->buffer_.AddAll(old0->buffer_);

        target.node_->SetLevel(height, newlnode);
        next->SetLevel(height, nullptr);
        next->DecHeight();
        delete old0;
        delete old1;
        target.node_->Rebound();
        next->Rebound();
      }
    }
  }

  virtual void GetStringSnapshot(std::vector<KVPair>& snapshot) const override {
    auto iter = s_.NewIterator();
    for (iter->SeekToFirst(); iter->Valid(); iter->Next())
      snapshot.emplace_back("", iter->Current().ToString());
    delete iter;
  }

  void UpdateTable(Statistable::TypeTime now, 
                   const LevelNode::VariableTable* gtable = nullptr,
                   std::vector<std::pair<Coordinates, double>>* market = nullptr) {
    size_t height = Current().height_;
    BFileVec& buffer = Current().Buffer();
    if (height == 0) return;
    size_t width = Current().Width();
    LevelNode::VariableTable& table = Current().Table();
    double time = 1.0 * head_->options_.TimeSliceMicroSecond() / 1000 / 1000;
    const SBSOptions& options = Current().node_->options_;
    
    table.ResetVariables();

    const Statistics* stats = Current().node_->GetTreeStatistics(height); 
    table[LocalGet]     = stats->GetStatistics(KSGetCount, now - 1) * 60 / time;
    table[LocalWrite]   = stats->GetStatistics(KSPutCount, now - 1) * 60 / time;
    table[LocalIterate] = stats->GetStatistics(KSIterateCount, now - 1) * 60 / time;
    table[LocalLeaf]    = stats->GetStatistics(LeafCount, -1);
    
    uint64_t bytes = stats->GetStatistics(KSBytesCount, STATISTICS_ALL);
    uint64_t entries = stats->GetStatistics(KSPutCount, STATISTICS_ALL);
    table[BytePerKey] = entries == 0 ? 1024 : bytes / entries;
    //assert(entries > 0);
    if (!gtable) gtable = &table;
      
    double gread = (*gtable)[LocalGet];
    double gwrite = (*gtable)[LocalWrite];
    double giterate = (*gtable)[LocalIterate];
    double gleaf = (*gtable)[LocalLeaf];
    uint64_t goperations = 1 + gread + gwrite + giterate * 100;

    double aread = gread / gleaf;
    double awrite = gwrite / gleaf;
    double aiterate = giterate / gleaf;
    
    table[GetPercent]     = 1000000ULL * table[LocalGet] / goperations;
    table[WritePercent]   = 1000000ULL * table[LocalWrite] / goperations;
    table[IteratePercent] = 1000000ULL * table[LocalIterate] / goperations;
    table[SpacePercent]   = 1000000ULL * table[LocalLeaf] / gleaf;
    
    table[MinHoleFileSize] = options.GlobalHoleFileSize();
    //if (total_operations >= 100 && table[WritePercent] < 10000) {
    //  if (height == 2)
    //    table[MinHoleFileSize] = 0;
    //} 

    BFileVec children;
    Current().node_->GetChildGuard(height, &children);
    for (BFile* file : buffer) {
      if (file->Type() == BFile::TypeHole) {
        table[HoleFileCount]++;
        table[HoleFileSize] += file->Data()->file_size;
        table[HoleFileRuns] += children.GetValueWidth(*file);
      } else {
        table[TapeFileCount]++;
        table[TapeFileSize] += file->Data()->file_size;
        table[TapeFileRuns] += children.GetValueWidth(*file);
      }
    }
    table[TotalFileCount] = table[HoleFileCount] + table[TapeFileCount];
    table[TotalFileSize]  = table[HoleFileSize]  + table[TapeFileSize];
    table[TotalFileRuns]  = table[HoleFileRuns]  + table[TapeFileRuns];

    {
    }
        
    if (market) {
      //double WWeight = (100.0 + table[LocalWrite]) / (100.0 + 1.0 * table[LocalWrite] + 0.2 * table[LocalGet] + 10000.0 * table[LocalIterate]);
      
      {
        double alpha = 0.5;
        double page_size = 4096;
        double cost[options.MaxWidth() + 2];
        
        double write = table[LocalWrite];
        double get = table[LocalGet];
        double iter = table[LocalIterate];
        double min_get = aread * table[LocalLeaf] * 0.01;
        if (get < min_get) get = min_get;
        double rsize = (*gtable)[BytePerKey];
        assert(rsize > 0);
        double B = page_size / rsize;
        double p = 0.001;
        double move_cost = page_size;
      
        double T = width;
        if (T < options.MinWidth() && height >= 3 && Current().node_->IsHead())
          T = Current().node_->GeneralWidth(height, 2);
        double base_wcost = 2 * 1 * (alpha + (height == 1 ? 1 : 0)) * T * write;
        double base_rcost = B * p * get + (B + move_cost) * iter;
        
        size_t max_runs = T; 
        table[HoleFileCapacity] = 100; 
        for (size_t i = 2; i <= max_runs; ++i) {
          double v = base_wcost / i - base_rcost;
          if (v <= 0) break;
          market->emplace_back(Current(), v);
        }
      }
    } else {
      table[HoleFileCapacity] = 800;
    }
    table[FileSizeScore] = 100ULL * table[HoleFileSize] / options.MaxFileSize() / options.MaxCompactionFiles();
    table[FileRunScore]  = 100ULL * table[HoleFileRuns] / width / options.MaxCompactionFiles();
    table[FileNumScore]  = 100ULL * table[HoleFileCount] / options.MaxCompactionFiles();
    //table[FileDynamicScore] = 100ULL * table[HoleFileSize] / options.MaxFileSize() / table[HoleFileCapacity];
    
    int emit = 0 + width - options.MaxWidth();
    if (emit < 0) emit = 0;
    
    table[NodeWidthScore] = 100ULL * emit / options.MaxWidth() * 2;
  }
  void UpdateAllTable() {
    Statistable::TypeTime now = head_->options_.NowTimeSlice();
    SeekToRoot();
    LevelNode::VariableTable& gtable = Current().Table();
    std::vector<std::pair<Coordinates, double>> market;
    UpdateTable(now);
    for (int height = SBSHeight() - 1; height > 0; --height) {
      SeekToRoot();
      for (Dive(SBSHeight() - 1 - height); Valid(); Next()) 
        //UpdateTable(now, &gtable, nullptr);
        UpdateTable(now, &gtable, &market);
    }
    std::sort(market.begin(), market.end(), 
      [](std::pair<Coordinates, double>& a, std::pair<Coordinates, double> b) {
        return a.second > b.second;});
    size_t data_size = gtable[LocalLeaf];// > 1000 ? gtable[LocalLeaf] : 1000;
    size_t capacity = 2.0 * data_size * head_->options_.SpaceAmplificationConst();
    size_t market_size = market.size();
    if (capacity > market.size()) 
      capacity = market.size();
    for (size_t i = 0; i < capacity; ++i) {
      auto &runs = market[i].first.Table()[HoleFileCapacity];
      runs += 100;
      //assert(runs <= head_->options_.MaxWidth());
    }
  }
};


}  
