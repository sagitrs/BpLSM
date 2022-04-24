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
namespace sagitrs {

struct SBSIterator;
struct Coordinates;
struct Scorer;

struct SBSNode : public Printable {
  typedef std::shared_ptr<SBSNode> SBSP;
  typedef std::shared_ptr<BoundedValue> ValuePtr;
  typedef LevelNode InnerNode;
  friend struct SBSIterator;
  friend struct Coordinates;
  friend struct Scorer;
 private:
  std::shared_ptr<SBSOptions> options_;
  bool is_head_;
  std::shared_ptr<BoundedValue> pacesetter_;
  std::vector<std::shared_ptr<InnerNode>> level_;
 public:
  // build head node.
  SBSNode(std::shared_ptr<SBSOptions> options, size_t height)
  : options_(options), 
    is_head_(true),
    pacesetter_(nullptr),
    level_({}) {
      for (size_t i = 0; i < height; ++i) {
        level_.push_back(std::make_shared<LevelNode>(std::dynamic_pointer_cast<StatisticsOptions>(options), nullptr));
      }
      Rebound();
    }
  // build leaf node.
  SBSNode(std::shared_ptr<SBSOptions> options, SBSP next) 
  : options_(options), 
    is_head_(false), 
    pacesetter_(nullptr), 
    level_({std::make_shared<LevelNode>(std::dynamic_pointer_cast<StatisticsOptions>(options), next)}) {}
  std::shared_ptr<BoundedValue> Pacesetter() const { return pacesetter_; }
  Slice Guard() const { 
    if (is_head_) return "";
    return Pacesetter()->Min(); 
  }
  size_t Height() const { return level_.size(); } 
  SBSP Next(size_t k, size_t recursive = 1) const { 
    SBSP next = level_[k]->next_;
    for (size_t i = 1; i < recursive; ++i) {
      assert(next != nullptr && next->Height() >= k);
      next = next->level_[k]->next_; 
    }
    return next;
  }
  std::shared_ptr<LevelNode> LevelAt(size_t height) const { return level_[height]; }
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
  void GetChildGuard(size_t height, BoundedValueContainer* container) const {
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
    if (level_[height - 1]->buffer_.empty())
      return 1;
    for (SBSP next = Next(height - 1); next != ed; next = next->Next(height - 1)) 
      if (next->level_[height - 1]->buffer_.empty())
        return 1;
    return 0;
  }
 private:
  void SetNext(size_t k, SBSP next) { level_[k]->next_ = next; }
  bool Overlap(size_t height, const Bounded& range) const {
    for (auto r : level_[height]->buffer_)
      if (r->Compare(range) == BOverlap) return true;
    return false;
  }
  void Rebound() {
    if (is_head_) {
      return;
    }
    for (auto node : level_)
      for (auto range : node->buffer_)
        if (pacesetter_ == nullptr || range->Min().compare(pacesetter_->Min()) < 0) { 
          pacesetter_ = range; 
        }
  }
  bool Empty() const {
    bool blank = true;
    for (auto node : level_)
      if (!node->buffer_.empty())
        return 0;
    return 1;
  }
 private:
  // return 1 if this node needs split.
  // return -1 if this node needs to absorb or to be absorbed.
  // return 0 if this node doesn't need change immediately.
  int TestState(const SBSOptions& options, size_t height) const { 
    if (height == 0) {
      if (level_[height]->buffer_.size() > 1) return 1;
      if (level_[height]->buffer_.size() == 0) {
        if (is_head_)
          return Next(0) && Next(0)->Height() == 1 ? -1 : 0;
        return -1;
      }
      return 0;
    }
    size_t width = Width(height);
    if (width > 50) {
      std::cout << "Warning : Width ambigous = " << width << std::endl;
    }
    return options.TestState(width, is_head_); 
  }
  bool Fit(size_t height, const Bounded& range, bool no_overlap) const { 
    Slice a(Guard()), b(Next(height)?Next(height)->Guard():"");
    Slice ra(range.Min()), rb(range.Max());
    int cmp1 = range.Min().compare(Guard());
    if (cmp1 < 0) return 0;
    auto next = Next(height);
    int cmp2 = next == nullptr ? -1 : range.Max().compare(next->Guard());
    if (cmp2 >= 0) return 0;
    if (!no_overlap) return 1;
    for (auto r : level_[height]->buffer_) {
      if (r->Compare(range) == BOverlap)
        return 0;
    }
    return 1;
  }
  void Add(const SBSOptions& options, size_t height, ValuePtr range) {
    level_[height]->Add(range);
    if (pacesetter_ == nullptr || Guard().compare(range->Min()) > 0)
      pacesetter_ = range;
  }
  ValuePtr Del(size_t height, ValuePtr range) {
    auto res = level_[height]->Del(range);
    if (Guard().compare(range->Min()) == 0)
      Rebound();
    return res;
  }
  void DecHeight() { level_.pop_back(); }
  std::shared_ptr<Statistable> GetNodeStatistics(size_t height) { return level_[height]->buffer_.GetStatistics(); }
  
  ValuePtr GetHottest(size_t height, int64_t time) {
    if (height == 0) 
      return level_[0]->buffer_.GetOne(); 
    
    auto &h = level_[height]->table_.hottest_;
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
  std::shared_ptr<Statistable> GetTreeStatistics(size_t height) {
    if (height == 0) 
      return level_[0]->buffer_.GetOne(); 
    
    auto s = level_[height]->table_.TreeStatistics();
    if (!level_[height]->table_.isDirty())
      return s;
    
    s->CopyStatistics(GetTreeStatistics(height - 1));
    for (SBSP i = Next(height - 1); i != Next(height); i = i->Next(height - 1)) 
      s->MergeStatistics(i->GetTreeStatistics(height - 1));
    s->MergeStatistics(GetNodeStatistics(height));
    
    level_[height]->table_.SetDirty(false);
    return s;
  }
 private:
 public:
  bool SplitNext(const SBSOptions& options, size_t height, BoundedValueContainer* force = nullptr) {
    if (height == 0) {
      auto &a = level_[0]->buffer_;
      assert(a.size() == 2);
      auto tmp = std::make_shared<SBSNode>(options_, Next(0));
      auto v = *a.rbegin();
      tmp->Add(options, 0, v);
      SetNext(0, tmp);
      Del(0, v);
      return 1;
    } else {
      //assert(!level_[height]->isDirty());
      size_t width = Width(height);
      assert(options.TestState(width, is_head_) > 0);
      size_t reserve = width - options.DefaultWidth();
      assert(reserve > 1);
      SBSP next = Next(height);
      SBSP middle = Next(height - 1, reserve);
      auto tmp = std::make_shared<LevelNode>(options_, next);
      {
        // Check dirty problem.
        RealBounded div(middle->Guard(), middle->Guard());
        for (auto& v : level_[height]->buffer_) {
          BCP cmp = v->Compare(div);
          if (cmp == BLess) {
            // reserve in current node.
          } else if (cmp == BGreater) {
            // move to next node. 
            tmp->Add(v);
            //level_[height]->Del(v);
          } else {
            // dirty.
            assert(cmp == BOverlap);
            assert(v->Min().compare(middle->Guard()) <= 0 
                && middle->Guard().compare(v->Max()) <= 0);
            if (!force)
              return 0;
            force->Add(v);
          }
        }
        for (auto& v : tmp->buffer_)
          level_[height]->Del(v);
        if (force)
          for (auto& v : *force)
            level_[height]->Del(v);
      }
      middle->level_.push_back(tmp); 
      SetNext(height, middle);
      // if this node is root node, increase height.
      if (is_head_ && height + 1 == Height()) {
        assert(false && "Error : try to increase tree height.");
        assert(next == nullptr);
        //IncHeight(level_[height]->node_stats_, nullptr);
      }
      return 1;
    }
  }
  void AbsorbNext(const SBSOptions& options, size_t height) {
    auto next = Next(height);
    assert(next != nullptr);
    assert(next->Height() == height+1);
    
    level_[height]->Absorb(next->level_[height]);
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
      level_[i]->GetStringSnapshot(snapshot);
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

/*
std::shared_ptr<BoundedValue> GetHottest(size_t height, size_t time) {
    if (height == 0) {
      if (level_[0]->buffer_.empty())
        return nullptr; 
      assert(level_[0]->buffer_.size() == 1); 
      return level_[0]->buffer_[0]; 
    } 
    auto &h = level_[height]->table_.hottest_;
    auto &t = level_[height]->table_.update_time_;
    if (h != nullptr && t == time)
      return h;
    h = GetHottest(height - 1, time);
    t = time;
    int64_t ri = (h == nullptr ? 0 : h->GetStatistics(KSGetCount, time));
    for (SBSP i = Next(height - 1); i != Next(height); i = i->Next(height - 1)) {
      auto tmp = i->GetHottest(height - 1, time);
      if (tmp == nullptr) continue;
      auto tmp_ri = tmp->GetStatistics(KSGetCount, time);
      if (h == nullptr || tmp_ri > ri) {
        h = tmp;
        ri = tmp_ri;
      }
    }
    return h;
  }

*/