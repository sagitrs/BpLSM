#pragma once

#include <vector>
#include <stack>
#include "sbs_node.h"

namespace sagitrs {
struct Scorer {
 private:
  struct GlobalStatus {
    size_t head_height_;
    std::shared_ptr<Statistable> global_stats_;
    Statistics::TypeTime time_;
    GlobalStatus(std::shared_ptr<SBSNode> head) {
      assert(head->is_head_);
      head_height_ = head->Height();
      global_stats_ = head->GetTreeStatistics(head_height_ - 1);
      time_ = head->options_->NowTimeSlice();
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
  virtual double GetScore(std::shared_ptr<SBSNode> node, size_t height) {
    SetNode(node, height);
    return Calculate();
  }
  virtual double ValueScore(std::shared_ptr<BoundedValue> value) { return ValueCalculate(value) / Capacity(); }
  bool isUpdated() const { return is_updated_; }
 
  virtual double ValueCalculate(std::shared_ptr<BoundedValue> value) { return 1; }
  virtual size_t Capacity() { return Width(); }
  virtual double Calculate() {
    double score = 0;
    for (auto value : Buffer())
      score += ValueCalculate(value);
    return score / Capacity();
  }
 // resources can be used.
 protected:
  void SetNode(std::shared_ptr<SBSNode> node, size_t height) { node_ = node; height_ = height; }
  size_t Height() const { return height_; }
  size_t Width() const { return node_->Width(height_); }
  BoundedValueContainer& Buffer() const { return node_->level_[height_]->buffer_; }
  size_t BufferSize() const { return node_->level_[height_]->buffer_.size(); }
  const GlobalStatus& Global() const { return *status_; }
  std::shared_ptr<Statistable> GetStatistics() { return node_->GetTreeStatistics(height_); }
  std::shared_ptr<BoundedValue> GetHottest(int64_t time) { return node_->GetHottest(height_, time); }
  std::shared_ptr<SBSOptions> Options() { return node_->options_; }
};

}