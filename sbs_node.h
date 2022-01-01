#pragma once

#include <vector>
#include <stack>
#include <memory>
#include "db/dbformat.h"
#include "bounded.h"
#include "bounded_value_container.h"
#include "sbs_options.h"

namespace sagitrs {
struct SBSNode;

typedef BoundedValueContainer TypeBuffer;
//TODO: Buffer shall be sorted!!!!!

struct LevelNode {
  std::shared_ptr<SBSNode> next_;
  TypeBuffer buffer_;
  LevelNode() : next_(nullptr), buffer_() {}
  LevelNode(std::shared_ptr<SBSNode> next) 
  : next_(next), buffer_() {}
  void Add(std::shared_ptr<BoundedValue> value) { buffer_.Add(value); }
  void Del(std::shared_ptr<BoundedValue> value) { buffer_.Del(*value); }
  bool Contains(std::shared_ptr<BoundedValue> value) const { return buffer_.Contains(*value); }
  bool Overlap() const { return buffer_.Overlap(); }
  void Absorb(const LevelNode& node) {
    next_ = node.next_;
    buffer_.AddAll(node.buffer_);
  }
  bool isDirty() const { return !buffer_.empty(); }
};

struct SBSNode {
  typedef std::shared_ptr<SBSNode> SBSP;
  typedef std::shared_ptr<BoundedValue> ValuePtr;

 private:
  bool is_head_;
  Slice guard_;
  std::vector<std::shared_ptr<LevelNode>> level_;
 public:
  // build head node.
  SBSNode()
  : is_head_(true),
    guard_(""),
    level_({std::make_shared<LevelNode>(), std::make_shared<LevelNode>()}) {}
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
      SetNext(height, middle);
      // if this node is root node, increase height.
      if (is_head_ && height + 1== Height()) {
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
  struct Iterator {
    using ExaminationFunction = double (*)(const SBSOptions& options, SBSP node);
    struct Coordinates {
      SBSP node_;
      size_t height_;
      Coordinates(SBSP node, size_t height) : node_(node), height_(height) {}
      
      SBSP Next() const { return node_->Next(height_); }
      void JumpNext() { node_ = Next(); }
      int TestState(const SBSOptions& options) const { return node_->TestState(options, height_); }
      bool Fit(const Bounded& range) const { return node_->Fit(height_, range); }
      void Del(ValuePtr range) const { node_->Del(height_, range); }
      void Add(const SBSOptions& options, ValuePtr range) const { node_->Add(options, height_, range); }
      bool Contains(ValuePtr range) const { return node_->level_[height_]->Contains(range); }
      void SplitNext(const SBSOptions& options) { node_->SplitNext(options, height_); }
      void AbsorbNext(const SBSOptions& options) { node_->AbsorbNext(options, height_); }
      bool operator ==(const Coordinates& b) { return node_ == b.node_; }
    };
   private:
    std::stack<Coordinates> history_;
    Coordinates& Current() { return history_.top(); }
   public:
    Iterator(SBSP head, int height = -1) 
    : history_() {
      history_.emplace(head, height < 0 ? head->Height()-1 : height);
    }
    void SeekToRoot() { while (history_.size() > 1) history_.pop(); }
    void SeekTree(const Bounded& range) {
      while (Current().Fit(range) && Current().height_ > 0) {
        SBSP st = Current().node_;
        SBSP ed = Current().Next();
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
    bool Seek(ValuePtr range) {
      while (history_.size() > 1 && !Current().Contains(range)) {
        history_.pop();
      } 
      return !(history_.size() > 1) 
          || Current().Contains(range);
    }
    void Add(const SBSOptions& options, ValuePtr range) {
      Current().Add(options, range);
      while (history_.size() >= 1) {
        if (Current().TestState(options) <= 0)
          break;
        Current().SplitNext(options);
        history_.pop();
      }
      SeekToRoot();
    }
    void Del(const SBSOptions& options, ValuePtr range) {
      Current().Del(range);
      while (history_.size() > 1) {
        Coordinates target = Current();
        history_.pop();

        bool shrink = Current().TestState(options) < 0;
        if (!shrink) break;

        SBSP st = Current().node_;
        SBSP ed = Current().Next();
        size_t h = target.height_;
        for (Coordinates i = Coordinates(st, h); i.node_ != ed; i.JumpNext())
          if (i.Next() != ed && i == target) {
            size_t prev_width = i.node_->Width(h);
            SBSP next = target.Next();
            if (next == ed) next = nullptr;
            if (next && next->Width(h) < prev_width) {
              i.JumpNext();
              assert(i == target);
            }
            i.AbsorbNext(options);
            if (i.TestState(options) > 0)
              i.SplitNext(options);
            break;
          }
      }
      SeekToRoot();
    }
  };
};


}  