#pragma once

#include <array>
#include "options.h"
#include "counter.h"

namespace sagitrs {

struct Statistics {
  std::vector<Counter> list_;
  typedef std::shared_ptr<Counter> CPTR;
 private:
  std::shared_ptr<StatisticsOptions> options_;
  std::deque<CPTR> history_;
 private:
  size_t CurrentTime() const {
    return (options_->Env()->NowMicros() - options_->begin_time_) / options_->time_slice_;
  }
  size_t LatestTimeSlice() const { return current_->slice_number_; }
  SPTR FindNumber(size_t number) const {
    for (auto iter = history_.rbegin(); iter != history_.rend(); iter ++) {
      int cmp = ((*iter)->slice_number_ - number);
      if (cmp == 0) return *iter;
      if (cmp < 0) return nullptr; 
    }
    return nullptr;
  }
  SPTR Latest(size_t diff) const {
    size_t number = LatestTimeSlice() - diff;
    SPTR res = FindNumber(number);
    if (res == nullptr) 
      return std::make_shared<BStatisticsSnapshot>(number);
    return res;
  }
 public:
  BStatistics(const BStatisticsOptions& options)
    : options_(options),
      history_(),
      current_(std::make_shared<BStatisticsSnapshot>(CurrentTimeSlice())),
      history_total_(std::make_shared<BStatisticsSnapshot>(0)),
      calculated_(nullptr) {}
      
  void Add(BStatisticsType type, uint64_t value) { 
    Tiktok();
    current_->Add(type, value); 
  }
  void AppendSnaphsot(SPTR s) {
    assert(history_.size() == 0 || history_.back()->slice_number_ < s->slice_number_);
    history_.push_back(s);
    *history_total_ += *s;
  }
  void CleanUpSnapshot() {
    while (!history_.empty()) {
      auto& head = history_.front();
      if (head->slice_number_ + options_.time_slice_size_ >= LatestTimeSlice())
        break;
      *head *= -1;
      *history_total_ += *head;
      history_.pop_front();
    }
  }
  void Tiktok() {
    size_t latest_slice_number = CurrentTimeSlice();
    if (latest_slice_number > current_->slice_number_) {
      AppendSnaphsot(current_);
      current_ = std::make_shared<BStatisticsSnapshot>(latest_slice_number);
      CleanUpSnapshot();
    }
  }

};

}