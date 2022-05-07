#pragma once

#include <sstream>
#include <vector>
#include <stack>
#include <memory>
#include "db/dbformat.h"
#include "bounded.h"
#include "bounded_value_container.h"
#include "options.h"
#include "statistics.h"
#include "level_node.h"
#include <atomic>
namespace sagitrs {

struct SBSIterator;
struct Coordinates;
struct Scorer;
struct SubSBS;

struct SBSNode : public Printable {
  typedef SBSNode* SBSP;
  typedef BFile* ValuePtr;
  friend struct SBSIterator;
  friend struct Coordinates;
  friend struct SubSBS;
  friend struct Scorer;
 private:
  SBSOptions options_;
  bool is_head_;

  std::atomic<int> height_;
  std::array<std::atomic<LevelNode*>, 6> level_;

  std::atomic<BFile*> pacesetter_;
 public:
  // build head node.
  SBSNode(const SBSOptions& options, size_t height)
  : options_(options), 
    is_head_(true), 
    height_(height),
    level_(),
    pacesetter_(nullptr) {
      for (size_t i = 0; i < height; ++i) {
        SetLevel(i, new LevelNode(options, nullptr));
      }
      Rebound();
    }
  // build leaf node.
  SBSNode(const SBSOptions& options, SBSP next) 
  : options_(options), 
    is_head_(false), 
    height_(1),
    level_(),
    pacesetter_(nullptr) {
      SetLevel(0, new LevelNode(options, next));
    }
  SBSNode(const SBSNode&) = delete;

  ~SBSNode() {
    for (size_t i = Height(); i > 0; --i)
      DecHeight();
  }

  bool IsHead() const { return is_head_; }
  BFile* Pacesetter() const { 
    return is_head_ ? nullptr : 
      pacesetter_.load(std::memory_order_acquire); 
  }
  void SetPacesetter(BFile* file) {
    pacesetter_.store(file, std::memory_order_release);
  }
  void SetLevel(size_t k, LevelNode* node) {
    level_[k].store(node, std::memory_order_release);
  }
  LevelNode* GetLevel(size_t height) const { 
    return level_[height].load(std::memory_order_acquire); 
  }
  Slice Guard() const { 
    if (is_head_) return "";
    return Pacesetter()->Min(); 
  }
  size_t Height() const { return height_.load(std::memory_order_acquire); } 
  void SetHeight(size_t h) { height_.store(h, std::memory_order_release); }
  SBSP Next(size_t k, size_t recursive = 1) const { 
    SBSP next = GetLevel(k)->next_.load(std::memory_order_acquire);
    for (size_t i = 1; i < recursive; ++i) {
      assert(next != nullptr && next->Height() >= k);
      next = next->GetLevel(k)->next_.load(std::memory_order_acquire); 
    }
    return next;
  }
 public:
  size_t Width(size_t height) const {
    if (height == 0) return 0;
    SBSP ed = Next(height);
    size_t width = 1;
    for (SBSP next = Next(height - 1); next != ed; next = next->Next(height - 1)) 
      width ++;
    return width;
  }
  size_t GeneralWidth(size_t height, size_t depth = 1) const {
    if (height < depth) return 0;
    SBSP ed = Next(height);
    size_t width = 1;
    for (SBSP next = Next(height - depth); next != ed; next = next->Next(height - depth)) 
      width ++;
    return width;
  }
  void GetChildGuard(size_t height, BFileVec* container) const {
    if (height == 0 || container == nullptr) return;
    SBSP ed = Next(height);
    if (Pacesetter()) container->push_back(Pacesetter());
    for (SBSP next = Next(height - 1); next != ed; next = next->Next(height - 1)) 
      if (next->Pacesetter())
        container->push_back(next->Pacesetter());
  }
  bool HasEmptyChild(size_t height) const {
    if (height == 0) return 0;
    SBSP ed = Next(height);
    if (GetLevel(height - 1)->buffer_.empty())
      return 1;
    for (SBSP next = Next(height - 1); next != ed; next = next->Next(height - 1)) 
      if (next->GetLevel(height - 1)->buffer_.empty())
        return 1;
    return 0;
  }
  void SetNext(size_t k, SBSP next) { 
    GetLevel(k)->next_.store(next,std::memory_order_release); 
  }
 private:
  bool Overlap(size_t height, const Bounded& range) const {
    for (auto r : GetLevel(height)->buffer_)
      if (r->Compare(range) == BOverlap) return true;
    return false;
  }
  void Rebound() {
    if (is_head_) {
      return;
    }
    BFile* pace = Pacesetter();
    BFile* res = pace;
    size_t h = Height();
    for (size_t i = 0; i < h; ++i)
      for (auto range : GetLevel(i)->buffer_)
        if (res == nullptr || range->Min().compare(res->Min()) < 0) { 
          res = range; 
        }
    if (pace != res)
      SetPacesetter(res);
  }
  bool Empty() const {
    bool blank = true;
    size_t h = Height();
    for (size_t i = 0; i < h; ++i)
      if (!GetLevel(i)->buffer_.empty())
        return 0;
    return 1;
  }
 private:
  // return 1 if this node needs split.
  // return -1 if this node needs to absorb or to be absorbed.
  // return 0 if this node doesn't need change immediately.
  int TestState(const SBSOptions& options, size_t height) const { 
    if (height == 0) {
      if (GetLevel(height)->buffer_.size() > 1) return 1;
      if (GetLevel(height)->buffer_.size() == 0) {
        if (is_head_)
          return Next(0) && Next(0)->Height() == 1 ? -1 : 0;
        return -1;
      }
      return 0;
    }
    size_t width = Width(height);
    if (width > 50) {
      std::cout << "Warning : Width ambigous = " << width 
        << "AT {" << Guard().ToString() << "," << height << "}" << std::endl;
    }
    return options.TestState(width, is_head_); 
  }
  inline bool Fit(size_t height, const Bounded& range, bool no_overlap) const { 
    //Slice a(Guard()), b(Next(height)?Next(height)->Guard():"");
    //Slice ra(range.Min()), rb(range.Max());
    int cmp1 = range.Min().compare(Guard());
    if (cmp1 < 0) return 0;
    auto next = Next(height);
    int cmp2 = next == nullptr ? -1 : range.Max().compare(next->Guard());
    if (cmp2 >= 0) return 0;
    if (!no_overlap) return 1;
    for (auto r : GetLevel(height)->buffer_) {
      if (r->Compare(range) == BOverlap)
        return 0;
    }
    return 1;
  }
  void Add(const SBSOptions& options, size_t height, ValuePtr file) {
    GetLevel(height)->Add(file);
    if (Pacesetter() == nullptr || Guard().compare(file->Min()) > 0)
      SetPacesetter(file);
  }
  BFile* Del(size_t height, const BFile& range) {
    auto res = GetLevel(height)->Del(range);
    if (Guard().compare(range.Min()) == 0)
      Rebound();
    res->SetDeletedLevel(height);
    return res;
  }
  void DecHeight() {
    size_t h = Height();
    assert(h > 0); 
    auto last = GetLevel(h - 1);
    SetHeight(h - 1);
    SetLevel(h - 1, nullptr);
    delete last;
  }
  void IncHeight(LevelNode* lnode) {
    size_t h = Height();
    SetLevel(h, lnode);
    SetHeight(h + 1);
  }
  const Statistics* GetNodeStatistics(size_t height) { return GetLevel(height)->buffer_.GetStatistics(); }
  
  BFile* GetHottest(size_t height, int64_t time) {
    if (height == 0) 
      return GetLevel(0)->buffer_.GetOne(); 
    
    auto &h = GetLevel(height)->table_.hottest_;
    if (h != nullptr)
      return h;

    h = GetHottest(height - 1, time);
    for (SBSP i = Next(height - 1); i != Next(height); i = i->Next(height - 1)) {
      auto ch = i->GetHottest(height - 1, time);
      if (h == nullptr || ch->GetStatistics(KSGetCount, time) > h->GetStatistics(KSGetCount, time))
        h = ch;
    }
    
    return h;
  }
  const Statistics* GetTreeStatistics(size_t height) {
    if (height == 0) 
      return GetLevel(0)->buffer_.GetStatistics();
    
    Statistics* s = GetLevel(height)->table_.stats_;
    if (!s) return s;
    
    std::vector<const Statistics*> ss;
    ss.push_back(GetTreeStatistics(height - 1));
    for (SBSP i = Next(height - 1); i != Next(height); i = i->Next(height - 1))
      ss.push_back(i->GetTreeStatistics(height - 1));
    ss.push_back(GetNodeStatistics(height));

    for (auto stat : ss) if (stat) {
      if (s == nullptr) 
        s = new Statistics(*stat);
      else 
        s->MergeStatistics(*stat);
    }
    
    GetLevel(height)->table_.SetDirty(false);
    return s;
  }
 private:
 public:
  bool SplitNext(const SBSOptions& options, size_t height, BFileVec* force = nullptr) {
    if (height == 0) {
      auto &a = GetLevel(0)->buffer_;
      assert(a.size() == 2);
      auto tmp = new SBSNode(options_, Next(0));
      auto v = *a.rbegin();
      tmp->Add(options, 0, v);
      SetNext(0, tmp);
      Del(0, *v);
      return 1;
    } else {
      //assert(!GetLevel(height)->isDirty());
      size_t width = Width(height);
      assert(options.TestState(width, is_head_) > 0);
      size_t reserve = width - options.DefaultWidth();
      assert(reserve > 1);
      SBSP next = Next(height);
      SBSP middle = Next(height - 1, reserve);
      auto tmp = new LevelNode(options_, next);
      {
        // Check dirty problem.
        RealBounded div(middle->Guard(), middle->Guard());
        for (auto& v : GetLevel(height)->buffer_) {
          BCP cmp = v->Compare(div);
          if (cmp == BLess) {
            // reserve in current node.
          } else if (cmp == BGreater) {
            // move to next node. 
            tmp->Add(v);
            //GetLevel(height)->Del(v);
          } else {
            // dirty.
            assert(cmp == BOverlap);
            assert(v->Min().compare(middle->Guard()) <= 0 
                && middle->Guard().compare(v->Max()) <= 0);
            if (!force) {
              delete tmp;
              return 0;
            }
            force->Add(v);
          }
        }
        for (auto& v : tmp->buffer_)
          GetLevel(height)->Del(*v);
        if (force)
          for (auto& v : *force)
            GetLevel(height)->Del(*v);
      }
      middle->IncHeight(tmp); 
      SetNext(height, middle);
      // if this node is root node, increase height.
      if (is_head_ && height + 1 == Height()) {
        assert(false && "Error : try to increase tree height.");
        assert(next == nullptr);
        //IncHeight(GetLevel(height)->node_stats_, nullptr);
      }
      return 1;
    }
  }
  void AbsorbNext(const SBSOptions& options, size_t height) {
    auto next = Next(height);
    assert(next != nullptr);
    assert(next->Height() == height+1);
    
    GetLevel(height)->Absorb(next->GetLevel(height));
    Rebound();
    next->DecHeight();
  }
 public:
  virtual void GetStringSnapshot(std::vector<KVPair>& snapshot) const override {
    assert(false);
  }
  void ForceUpdateStatistics() {
    assert(is_head_);
    auto stat = GetTreeStatistics(Height() - 1);
  }
  // make sure all tree stats are NOT dirty.
  virtual std::string ToString() const override {
    std::stringstream ss;
    size_t width = 20;
    std::vector<std::string> info[Height()];
    size_t max_lines = 0;
    for (size_t i = 0; i < Height(); ++i) {
      std::vector<KVPair> snapshot;
      GetLevel(i)->GetStringSnapshot(snapshot);
      for (auto& kv : snapshot) info[i].emplace_back(kv.first+"="+kv.second);
      if (info[i].size() > max_lines) max_lines = info[i].size();
    }
    for (size_t i = 0; i < max_lines; ++i) {
      for (size_t j = 0; j < Height(); ++j) {
        const std::string &data = i < info[j].size() ? info[j][i] : "";
        std::string suffix(data.size() > width ? 0 : width - data.size(), ' ');
        ss << data << suffix << "|";
      }
      ss << std::endl;
    }
    std::string divider((width+1)*Height()+1, '-');
    ss << divider << std::endl;
    return ss.str();
  }
};


}  
