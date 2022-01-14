#pragma once

#include <map>
#include <array>
#include <memory>
#include "bounded.h"
#include "options.h"
#include "leveldb/env.h"

namespace sagitrs {


struct Counter : public std::array<int64_t, DefaultCounterTypeMax> {
  using std::array<int64_t, DefaultCounterTypeMax>::operator[];
  void clear() {
    for (size_t i = 0; i < DefaultCounterTypeMax; ++i)
      std::array<int64_t, DefaultCounterTypeMax>::operator[i] = 0;
  }
  Counter& operator+=(const Counter& counter) {
    for (size_t i = 0; i < DefaultCounterTypeMax; ++i)
      std::array<int64_t, DefaultCounterTypeMax>::operator[i] += counter[i];
    return *this;
  }
  Counter& operator*=(int k) {
    if (k != 1)
      for (size_t i = 0; i < DefaultCounterTypeMax; ++i)
        std::array<int64_t, DefaultCounterTypeMax>::operator[](i) *= k;
    return *this;
  }
  Counter& operator/=(int k) {
    assert(k != 0);
    if (k != 1)
      for (size_t i = 0; i < DefaultCounterTypeMax; ++i)
        std::array<int64_t, DefaultCounterTypeMax>::operator[](i) /= k;
    return *this;
  }
};

struct TTLQueue : public std::vector<Counter> {
  int64_t ttl_;
  int64_t create_time_, st_time_, ed_time_;
  TTLQueue(int64_t ttl, int64_t time) 
  : std::vector<Counter>(ttl),
    ttl_(ttl),
    create_time_(time),
    st_time_(time), ed_time_(time) {}
  bool TimeLegal(int64_t time) const { return st_time_ <= time && time <= ed_time_; }
  int64_t size() const { return ed_time_ - st_time_ + 1; }
  int space() const { return ttl_ - size(); }
  bool isFull() const { return space() == 0; }
  void PopBack(size_t recursive = 1) { st_time_ += recursive; }
  void PushFront(int64_t time, const Counter& counter) {
    assert(time + 1 == st_time_);
    if (isFull()) return;
    st_time_ --;
    (*this)[time] = counter;
  }
  void PushBack(int64_t time, const Counter& counter) {
    if (isFull()) return;
    assert(time == ed_time_ + 1);
    (*this)[time] = counter;
    ed_time_ = time;
  }
  void Push(int64_t time, const Counter& counter) {
    if (time <= ed_time_) {
      PushFront(time, counter);
      return; 
    }
    if (time > ed_time_ + space()) 
      PopBack(time - ed_time_ - space());
    if (time > ed_time_ + 1) {
      Counter blank;
      for (auto t = ed_time_ + 1; t < time; ++t)
        PushBack(t, blank);
    }
    PushBack(time, counter);
  }
  void Merge(const TTLQueue& queue) {
    assert(ed_time_ == queue.ed_time_);
    for (auto t = queue.ed_time_; t >= queue.st_time_; --t) {
      if (t >= st_time_)
        (*this)[t] += queue[t];
      else 
        PushBack(t, queue[t]);
    }
  }
  Counter& operator[](int64_t time) {
    assert(st_time_ <= time && time <= ed_time_);
    return std::vector<Counter>::operator[](time % ttl_);
  }
  const Counter& operator[](int64_t time) const {
    assert(st_time_ <= time && time <= ed_time_);
    return std::vector<Counter>::operator[](time % ttl_);
  }
};

struct Statistics : public Statistable {
 private:
  std::shared_ptr<StatisticsOptions> options_;
  TTLQueue queue_;
  Counter history_;
 public:
  Statistics(std::shared_ptr<StatisticsOptions> options, TypeTime time) 
  : options_(options), 
    queue_(options_->TimeSliceMaximumSize(), time),
    history_() {}
  Statistics(const Statistics& src) = default;
  virtual void UpdateTime(TypeTime time) override {
    Counter blank;
    queue_.Push(time, blank);
  }
  virtual void UpdateStatistics(TypeLabel label, TypeData diff, TypeTime time) override {
    UpdateTime(time);
    queue_[time][label] += diff;
    history_[label] += diff;
  }
  virtual TypeData GetStatistics(TypeLabel label, TypeTime time = STATISTICS_ALL) override {
    switch (time) {
    case STATISTICS_ALL:
      return history_[label];
    default:
      assert(queue_.TimeLegal(time));
      return queue_[time][label];
    }
  }
  virtual void MergeStatistics(const Statistics& target) {
    if (target.queue_.ed_time_ > queue_.ed_time_)
      UpdateTime(target.queue_.ed_time_);
    queue_.Merge(target.queue_);
    history_ += target.history_;
  }
  virtual void ScaleStatistics(int numerator, int denominator) override {
    for (auto t = queue_.st_time_; t <= queue_.ed_time_; ++t) {
      queue_[t] *= numerator;
      queue_[t] /= denominator;
    }
    history_ *= numerator;
    history_ /= denominator;
  }
  virtual ~Statistics() {}
 };

}