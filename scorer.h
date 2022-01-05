#pragma once

#include <vector>
#include <stack>
#include "sbs_node.h"

namespace sagitrs {

struct Scorer {
 private:
 private:
  std::shared_ptr<SBSNode> node_;
  size_t height_;
 public:
  Scorer() :node_(nullptr), height_(0) {}
  void SetNode(std::shared_ptr<SBSNode> node, size_t height) { node_ = node; height_ = height; }
  size_t Height() const { return height_; }
  size_t Width() const { return node_->Width(height_); }
  size_t BufferSize() const { return node_->level_[height_]->buffer_.size(); }

  virtual double Calculate(std::shared_ptr<SBSNode> node, size_t height) = 0;
};

struct LeveledScorer : public Scorer {
 private:
  struct GlobalStatus {
    size_t head_height_;
  } status_;
  void Init(std::shared_ptr<SBSNode> head) {  status_.head_height_ = head->Height();  }
  size_t max_level0_size_ = 8;
  size_t max_tiered_runs_ = 1;
  size_t max_files_ = 20;
 public:
  LeveledScorer(std::shared_ptr<SBSNode> head) { Init(head); }
  size_t Level() const { return status_.head_height_ - Height(); }
  virtual double Calculate(std::shared_ptr<SBSNode> node, size_t height) override {
    Scorer::SetNode(node, height);
    if (Level() == 0) 
      return BufferSize() > max_level0_size_ ? 1 : 0;
    if (BufferSize() > max_tiered_runs_)
      return 1;
    return 1.0 * (BufferSize() + Width()) / max_files_;
  }
};

}