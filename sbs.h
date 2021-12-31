#pragma once

#include "db/dbformat.h"
#if defined(WITH_BVERSION)
#include <vector>
#include "binterface.h"
#include <stack>

namespace leveldb {

struct RMSOptions {
  size_t width_[3] = {2, 4, 8};
  size_t MinWidth() const { return width_[0]; }
  size_t DefaultWidth() const { return width_[1]; }
  size_t MaxWidth() const { return width_[2]; }
  int TestState(size_t size, bool lbound_ignore) const {
    if (!lbound_ignore && size < MinWidth()) return -1;
    if (size > MaxWidth()) return 1;
    return 0;
  }
};

struct RBSNode {
  typedef std::shared_ptr<RBSNode> RBSP;
  typedef std::shared_ptr<BBounded> BP;
  struct LevelNode {
    RBSP next_;
    std::vector<BP> buffer_;
    LevelNode(RBSP next = nullptr, const std::vector<BP>& buffer = {}) 
    : next_(next), buffer_(buffer) {}
    void Add(BP range) { buffer_.push_back(range); }
    void Del(const BBounded& range) { 
      for (auto iter = buffer_.begin(); iter != buffer_.end(); iter ++)
        if (**iter == range) {
          buffer_.erase(iter);
          break;
        }
    }
    void Absorb(const LevelNode& node) {
      next_ = node.next_;
      buffer_.insert(buffer_.end(), node.buffer_.begin(), node.buffer_.end());
    }
    bool isDirty() const { return !buffer_.empty(); }
  };
 private:
  bool is_head_;
  Slice guard_;
  std::vector<std::shared_ptr<LevelNode>> level_;
 public:
  // build head node.
  RBSNode()
  : is_head_(true),
    guard_(""),
    level_(std::make_shared<LevelNode>(nullptr, {})) {}
 private:
  // build leaf node with file.
  RBSNode(BP range, RBSP next) 
  : is_head_(false), 
    guard_(range->Min()), 
    level_(std::make_shared<LevelNode>(next, {range})) {}
 private:
  Slice Guard() const { return is_head_ ? Slice("") : guard_; }
  size_t Height() const { return level_.size(); } 
  RBSP Next(size_t k, size_t recursive = 0) const { 
    RBSP next = level_[k]->next_;
    for (size_t i = 1; i < recursive; ++i) {
      assert(next != nullptr && next->Height() >= k);
      next = next->level_[k]->next_; 
    }
    return next;
  }
  void SetNext(size_t k, RBSP next) { level_[k]->next_ = next; }
  size_t Width(size_t height) const {
    if (height == 0) return 0;
    RBSP ed = Next(height);
    size_t width = 1;
    for (RBSP next = Next(height - 1); next != ed; next = Next(height - 1)) 
      width ++;
    return width;
  }
  bool Overlap(size_t height, const BBounded& range) const {
    for (auto r : level_[height]->buffer_)
      if (r->Compare(range) == BOverlap) return true;
    return false;
  }
  void Rebound() const {
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
  int TestState(const RMSOptions& options, size_t height) const { 
    return options.TestState(Width(height), is_head_); 
  }
  bool Fit(size_t height, const BBounded& range) const { 
    return Next(height) == nullptr 
        || range.Max().compare(Next(height)->Guard()) < 0; 
  }
  void Add(const RMSOptions& options, size_t height, BP range) {
    if (height == 0 && !Overlap(height, *range)) {
      SplitNext(options, range);
      return;
    }
    level_[height]->Add(range);
    if (Guard().compare(range->Min()) > 0)
      guard_ = range->Min();
  }
  void Del(size_t height, const BBounded& range) {
    level_[height]->Del(range);
    if (Guard().compare(range.Min()) == 0)
      Rebound();
  }
  void IncHeight(RBSP next = nullptr, const std::vector<BP>& buffer = {}) { level_.emplace_back(next, buffer); }
  void DecHeight() { level_.pop_back(); }
  void SplitNext(const RMSOptions& options, size_t height) {
    assert(height > 0);
    assert(!isDirty());
    assert(options.TestState(Width(), is_head_) > 0);
    size_t reserve = options.MinWidth();
    assert(reserve > 1);
    RBSP next = Next(height);
    RBSP middle = Next(height - 1, reserve - 1);
    middle->IncHeight(next);
  }
  void SplitNext(const RMSOptions& options, BP range) {
    assert(height_ == 0);
    auto tmp = std::make_shared<RBSNode>(range, Next(0));
    SetNext(0, tmp);
  }
  void AbsorbNext(const RMSOptions& options, size_t height) {
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
    size_t std_total_width = 30;
    for (const RBSNode* i = this; i != nullptr; i = i->Next(0).get()) {
      std::string divider(std_total_width,'-');
      ss << divider;
      
      for (size_t k = 0;; k++) {
        std::vector<std::string> line;
        bool line_null = false;
        for (size_t h = 0; h < i->level_.size(); ++h) {
          auto& buf = i->level_[h]->buffer_;
          if (buf.size() > k) {
            line.push_back(buf[k]->ToString());
            line_null = ;
          }
          else
            line.push_back(""); 
        }
        bool null
      }
    }
  }
  struct Iterator {
    using ExaminationFunction = double (*)(const RMSOptions& options, RBSP node);
    struct Coordinates {
      RBSP node_;
      size_t height_;
      Coordinates(RBSP node, size_t height) : node_(node), height_(height) {}
      
      RBSP Next() const { return node_->Next(height_); }
      void JumpNext() { node_ = Next(); }
      int TestState(const RMSOptions& options) const { return node_->TestState(options, height_); }
      bool Fit(const BBounded& range) const { return node_->Fit(height_, range); }
      void Del(const BBounded& range) const { node_->Del(height_, range); }
      void Add(const RMSOptions& options, BP range) const { node_->Add(options, height_, range); }
      void SplitNext(const RMSOptions& options) { node_->SplitNext(options, height_); }
      void AbsorbNext(const RMSOptions& options) { node_->AbsorbNext(options, height_); }
      bool operator ==(const Coordinates& b) { return node_ == b.node_; }
    };
   private:
    std::stack<Coordinates> history_;
    Coordinates& Current() { return history_.top(); }
   public:
    Iterator(RBSP head, int height) 
    : history_() {
      history_.emplace(head, height);
    }
    void SeekToRoot() { while (history_.size() > 1) history_.pop(); }
    void Seek(const BBounded& range) {
      while (Current().Fit(range) && Current().height_ > 0) {
        RBSP st = Current().node_;
        RBSP ed = Current().Next();
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
    void Add(const RMSOptions& options, BP range) {
      Current().Add(options, range);
      while (history_.size() > 1) {
        history_.pop();
        assert(!history_.empty());
        if (Current().TestState(options) <= 0)
          break;
      }
      SeekToRoot();
    }
    void Del(const RMSOptions& options, const BBounded& range) {
      Current().Del(range);
      while (history_.size() > 1) {
        Coordinates target = Current();
        history_.pop();

        bool shrink = Current().TestState(options) < 0;
        if (!shrink) break;

        RBSP st = Current().node_;
        RBSP ed = Current().Next();
        size_t h = target.height_;
        for (Coordinates i = Coordinates(st, h); i.node_ != ed; i.JumpNext())
          if (i.Next() != ed && i == target) {
            size_t prev_width = i.node_->Width(h);
            RBSP next = target.Next();
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

  }
};


}  // namespace leveldb

#endif  
