#pragma once

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

struct SBSNode {
  typedef std::shared_ptr<SBSNode> SBSP;
  typedef std::shared_ptr<BoundedValue> ValuePtr;
  typedef LevelNode InnerNode;
  friend struct SBSIterator;
  friend struct Coordinates;
 private:
  bool is_head_;
  Slice guard_;
  std::vector<std::shared_ptr<InnerNode>> level_;
 public:
  // build head node.
  SBSNode()
  : is_head_(true),
    guard_(""),
    level_({std::make_shared<LevelNode>(), std::make_shared<LevelNode>()}) {}
  // build leaf node.
  SBSNode(SBSP next) 
  : is_head_(false), 
    guard_("Undefined."), 
    level_({std::make_shared<LevelNode>(next)}) {}
  Slice Guard() const { return is_head_ ? Slice("") : guard_; }
  size_t Height() const { return level_.size(); } 
  SBSP Next(size_t k, size_t recursive = 1) const { 
    SBSP next = level_[k]->next_;
    for (size_t i = 1; i < recursive; ++i) {
      assert(next != nullptr && next->Height() >= k);
      next = next->level_[k]->next_; 
    }
    return next;
  }
 private:
  void SetNext(size_t k, SBSP next) { level_[k]->next_ = next; }
  size_t Width(size_t height) const {
    if (height == 0) return 0;
    SBSP ed = Next(height);
    size_t width = 1;
    for (SBSP next = Next(height - 1); next != ed; next = next->Next(height - 1)) 
      width ++;
    return width;
  }
  bool Overlap(size_t height, const Bounded& range) const {
    for (auto r : level_[height]->buffer_)
      if (r->Compare(range) == BOverlap) return true;
    return false;
  }
  void Rebound() {
    bool blank = true;
    for (auto node : level_) {
      for (auto range : node->buffer_) {
        if (blank) { 
          guard_ = range->Min(); 
          blank = 0; 
        } else if (range->Min().compare(guard_) < 0) {
          guard_ = range->Min();
        }
      }
    }
    assert(!blank);
  }
 private:
  int TestState(const SBSOptions& options, size_t height) const { 
    if (height == 0)
      return level_[height]->buffer_.size() > 1;
    else 
      return options.TestState(Width(height), is_head_); 
  }
  bool Fit(size_t height, const Bounded& range) const { 
    int cmp1 = range.Min().compare(Guard());
    if (cmp1 < 0) return 0;
    auto next = Next(height);
    if (next == nullptr) return 1;
    int cmp2 = range.Max().compare(next->Guard());
    return cmp2 < 0;
  }
  void Add(const SBSOptions& options, size_t height, ValuePtr range) {
    level_[height]->Add(range);
    if (Guard().compare(range->Min()) > 0)
      guard_ = range->Min();
  }
  void Del(size_t height, ValuePtr range) {
    level_[height]->Del(range);
    if (Guard().compare(range->Min()) == 0)
      Rebound();
  }
  void IncHeight(SBSP next) { 
    level_.push_back(std::make_shared<LevelNode>(next)); 
  }
  void DecHeight() { level_.pop_back(); }
  void SplitNext(const SBSOptions& options, size_t height) {
    if (height == 0) {
      auto &a = level_[0]->buffer_;
      assert(a.size() == 2);
      auto tmp = std::make_shared<SBSNode>(Next(0));
      tmp->level_[0]->BuildBlankParaTable(*level_[0]->paras_);
      tmp->Add(options, 0, *a.rbegin());
      SetNext(0, tmp);
      a.Del(**a.rbegin());
    } else {
      assert(!level_[height]->isDirty());
      assert(options.TestState(Width(height), is_head_) > 0);
      size_t reserve = options.DefaultWidth();
      assert(reserve > 1);
      SBSP next = Next(height);
      SBSP middle = Next(height - 1, reserve);
      middle->IncHeight(next);
      middle->level_[height]->BuildBlankParaTable()
      SetNext(height, middle);
      // if this node is root node, increase height.
      if (is_head_ && height + 1 == Height()) {
        assert(next == nullptr);
        IncHeight(nullptr);
      }
    }
  }
  void AbsorbNext(const SBSOptions& options, size_t height) {
    auto next = Next(height);
    assert(next != nullptr);
    assert(next->Height() == height);
    level_[height]->Absorb(*next->level_[height]);
    next->DecHeight();
  }
 public:
  std::string ToString() const {
    std::stringstream ss;
    size_t width = 5;
    size_t std_total_width = 42;
    {
      TypeBuffer::const_iterator iters[Height()], iters_end[Height()];
      for (size_t i = 0; i < Height(); ++i) {
        iters[i] = level_[i]->buffer_.begin();
        iters_end[i] = level_[i]->buffer_.end();
      }
      for (size_t k = 0;; k++) {
        std::vector<std::string> line;
        bool line_null = true;
        for (size_t h = 0; h < level_.size(); ++h) {
          if (iters[h] != iters_end[h]) {
            line.push_back((*iters[h])->ToString());
            iters[h]++;
            line_null = false;
          }
          else
            line.push_back(""); 
        }
        if (line_null) 
          break;
        for (size_t i = 0; i < line.size(); ++i) {
          std::string suffix(line[i].size() > width ? 0 : width - line[i].size(), ' ');
          ss << line[i] << suffix << "|";
        }
        ss << std::endl;
      }
    }
    return ss.str();
  }
};


}  