#pragma once

#include <vector>
#include <stack>
#include "sbs_node.h"

namespace sagitrs {
struct Scorer {
 private:
  struct GlobalStatus {
    size_t head_height_;
    std::shared_ptr<Statistics> global_stats_;
    GlobalStatus(std::shared_ptr<SBSNode> head) {
      assert(head->is_head_);
      head_height_ = head->Height();
      global_stats_ = std::make_shared<Statistics>(head->options_);

      auto& nst = head->level_[head_height_ - 1]->node_stats_;
      auto& cst = head->level_[head_height_ - 1]->child_stats_;
      if (nst) global_stats_->Superposition(*nst);
      if (cst) global_stats_->Superposition(*cst);
    }
  };
  std::shared_ptr<GlobalStatus> status_;
  bool is_updated_;
  double max_score_;

  std::shared_ptr<SBSNode> node_;
  size_t height_;
 public:
  Scorer() 
  : status_(nullptr), is_updated_(false), max_score_(0), 
    node_(nullptr), height_(0) {}
  virtual void Init(std::shared_ptr<SBSNode> head) { 
    status_ = std::make_shared<GlobalStatus>(head); 
  }
  virtual void Reset(double baseline) { 
    is_updated_ = 0;
    max_score_ = baseline;
  }
  virtual double MaxScore() const { return max_score_; }
  virtual bool Update(std::shared_ptr<SBSNode> node, size_t height) {
    //if (max_score_ == 1) return 0;
    SetNode(node, height);
    double score = GetScore(node, height);
    if (score > max_score_) {
      max_score_ = score;
      is_updated_ = 1;
      return 1;
    }
    return 0;
  }
  double GetScore(std::shared_ptr<SBSNode> node, size_t height) {
    SetNode(node, height);
    return Calculate();
  }
  bool isUpdated() const { return is_updated_; }
 protected:
  void SetNode(std::shared_ptr<SBSNode> node, size_t height) { node_ = node; height_ = height; }
  virtual double Calculate() = 0;
  size_t Height() const { return height_; }
  size_t Width() const { return node_->Width(height_); }
  size_t BufferSize() const { return node_->level_[height_]->buffer_.size(); }
  const GlobalStatus& Global() const { return *status_; }
  int64_t GetStatistics(StatisticsType stype, DefaultCounterType label) {
    int64_t res = 0;
    if (node_->level_[height_]->node_stats_)
      res += node_->level_[height_]->node_stats_->Get(stype, label);
    if (node_->level_[height_]->child_stats_)
      res += node_->level_[height_]->child_stats_->Get(stype, label);
    return res;
  }
};

}