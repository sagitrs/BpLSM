#pragma once

#include <vector>
#include <stack>
#include "sbs_node.h"

namespace sagitrs {
struct Scorer {
 private:
  struct GlobalStatus {
    size_t head_height_;
    Statistics* global_stats_;
    Statistable::TypeTime time_;
    GlobalStatus(SBSNode* head) {
      assert(head->is_head_);
      head_height_ = head->Height();
      auto head_stats = head->GetTreeStatistics(head_height_ - 1);
      global_stats_ = head_stats ? new Statistics(*head_stats) : nullptr;
      time_ = head->options_.NowTimeSlice();
    }
    ~GlobalStatus() { 
      if (global_stats_)
        delete global_stats_;
    }
  };
  GlobalStatus* status_;
  bool is_updated_;
  double max_score_;

  SBSNode* node_;
  size_t height_;
 public:
  Scorer() 
  : status_(nullptr), is_updated_(false), max_score_(0), 
    node_(nullptr), height_(0) {}
  virtual void Init(SBSNode* head) { 
    status_ = new GlobalStatus(head); 
  }
  ~Scorer() {
    if (status_) delete status_;
  }
  virtual void Reset(double baseline) { 
    is_updated_ = 0;
    max_score_ = baseline;
  }
  virtual double MaxScore() const { return max_score_; }
  virtual bool Update(SBSNode* node, size_t height) {
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
  virtual double GetScore(SBSNode* node, size_t height) {
    SetNode(node, height);
    return Calculate();
  }
  virtual double ValueScore(BFile* value) { return ValueCalculate(value) / Capacity(); }
  bool isUpdated() const { return is_updated_; }
 
  virtual void TreeInit() {}
  virtual double ValueCalculate(BFile* value) { return 1; }
  virtual double Capacity() { return Width(); }
  virtual double Calculate() {
    double score = 0;
    TreeInit();
    for (auto value : Buffer())
      score += ValueCalculate(value);
    return score / Capacity();
  }
 // resources can be used.
 protected:
  void SetNode(SBSNode* node, size_t height) { node_ = node; height_ = height; }
  size_t Height() const { return height_; }
  size_t Width(size_t depth = 1) const { 
    size_t res = node_->GeneralWidth(height_, depth); 
    //if (res < Options()->MinWidth())
    //  res = node_->GeneralWidth(height_, 2);
    return res;
  }
  BFileVec& Buffer() const { return node_->GetLevel(height_)->buffer_; }
  size_t BufferSize() const { return node_->GetLevel(height_)->buffer_.size(); }
  const GlobalStatus& Global() const { return *status_; }
  const Statistics& GetStatistics() { 
    return *node_->GetTreeStatistics(height_); 
  }
  BFile* GetHottest(int64_t time) { 
    return node_->GetHottest(height_, time); 
  }
  const SBSOptions& Options() const { return node_->options_; }
  void GetChildren(sagitrs::BFileVec* children) {
    node_->GetChildGuard(height_, children);
  }
};

}