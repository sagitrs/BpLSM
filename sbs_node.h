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

struct SBSNode {
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
  Slice Guard() const { 
    if (is_head_) return "";
    return pacesetter_->Min(); 
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
  int TestState(const SBSOptions& options, size_t height) const { 
    if (height == 0) {
      if (level_[height]->buffer_.size() > 1) return 1;
      if (!is_head_ && level_[height]->buffer_.size() == 0) return -1;
      return 0;
    }
    else 
      return options.TestState(Width(height), is_head_); 
  }
  bool Fit(size_t height, const Bounded& range, bool no_overlap) const { 
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
  void Del(size_t height, ValuePtr range) {
    level_[height]->Del(range);
    if (Guard().compare(range->Min()) == 0)
      Rebound();
  }
  void IncHeight(std::shared_ptr<Statistics> stats, SBSP next) { 
    auto tmp = std::make_shared<LevelNode>(stats->options_, next);
    level_.push_back(tmp); 
    auto &node = *level_.rbegin();
    node->node_stats_->Inherit(stats, Guard());
  }
  void DecHeight() { level_.pop_back(); }
  void SplitNext(const SBSOptions& options, size_t height) {
    if (height == 0) {
      auto &a = level_[0]->buffer_;
      assert(a.size() == 2);
      auto tmp = std::make_shared<SBSNode>(options_, Next(0));
      tmp->Add(options, 0, *a.rbegin());
      tmp->level_[0]->node_stats_->Inherit(level_[0]->node_stats_, tmp->Guard());
      SetNext(0, tmp);
      a.Del(**a.rbegin());
    } else {
      assert(!level_[height]->isDirty());
      size_t width = Width(height);
      assert(options.TestState(width, is_head_) > 0);
      size_t reserve = width - options.DefaultWidth();
      assert(reserve > 1);
      SBSP next = Next(height);
      SBSP middle = Next(height - 1, reserve);
      middle->IncHeight(level_[height]->node_stats_, next);
      //middle->level_[height]->node_stats_->Inherit(level_[0]->node_stats_, tmp->Guard());
      SetNext(height, middle);
      // if this node is root node, increase height.
      if (is_head_ && height + 1 == Height()) {
        assert(false && "Error : try to increase tree height.");
        assert(next == nullptr);
        IncHeight(level_[height]->node_stats_, nullptr);
      }
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
  enum PrintType { DataInfo, StatInfo };
  std::string ToString(PrintType type) const {
    std::stringstream ss;
    size_t width = 10;
    size_t std_total_width = 42;
    if (type == DataInfo) {
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
    } else if (type == StatInfo) {
      for (size_t i = 0; i < Height(); ++i) {
        std::string data = std::to_string(level_[i]->node_stats_->Get(TypeHistorical, DefaultCounterType::PutCount));
        std::string suffix(data.size() > width ? 0 : width - data.size(), ' ');
        ss << data << suffix;
      }
      ss << std::endl;
    }
    return ss.str();
  }
  std::string ToString() const {
    std::stringstream ss;
    size_t width = 16;
    std::vector<std::string> info[Height()];
    size_t max_lines = 0;
    for (size_t i = 0; i < Height(); ++i) {
      level_[i]->GetStringLog(info[i]);
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