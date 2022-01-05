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

}