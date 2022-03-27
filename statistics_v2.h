#pragma once

#include <map>
#include <array>
#include <memory>
#include "bounded.h"
#include "options.h"
#include "leveldb/env.h"

namespace sagitrs {

struct Counter : public std::array<int64_t, DefaultCounterTypeMax>, public Printable {
  Counter() : std::array<int64_t, DefaultCounterTypeMax>() {}
  using std::array<int64_t, DefaultCounterTypeMax>::operator[];
  void clear() {
    for (size_t i = 0; i < DefaultCounterTypeMax; ++i)
      std::array<int64_t, DefaultCounterTypeMax>::operator[](i) = 0;
  }
  inline void Merge(const Counter& counter, uint32_t label) {
    std::array<int64_t, DefaultCounterTypeMax>::operator[](label) += counter[label];
  }
  inline void Scale(uint32_t label, int n, int m) {
    std::array<int64_t, DefaultCounterTypeMax>::operator[](label) *= n;
    std::array<int64_t, DefaultCounterTypeMax>::operator[](label) /= m;
  }
  Counter& operator+=(const Counter& counter) {
    for (size_t i = 0; i < DefaultCounterTypeMax; ++i)
      std::array<int64_t, DefaultCounterTypeMax>::operator[](i) += counter[i];
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
  virtual void GetStringSnapshot(std::vector<KVPair>& snapshot) const override {
    for (size_t i = 0; i < DefaultCounterTypeMax; ++i)
      snapshot.emplace_back("C["+std::to_string(i)+"]", std::to_string(operator[](i)));
  }
  void Clear() {
    for (size_t i = 0; i < DefaultCounterTypeMax; ++i)
      std::array<int64_t, DefaultCounterTypeMax>::operator[](i) = 0;
  }
};
struct TTLQueue : public std::vector<Counter>, public Printable {
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
  void PopFront(size_t recursive = 1) { 
    st_time_ += recursive; 
    if (st_time_ > ed_time_) {
      ed_time_ = st_time_;
      operator[](ed_time_).Clear();
    }
      
  }
  void PushFront(int64_t time, const Counter& counter) {
    assert(time + 1 == st_time_);
    if (isFull()) return;
    st_time_ --;
    (*this)[time] = counter;
  }
  void PushBack(int64_t time, const Counter& counter) {
    if (isFull()) return;
    assert(time == ed_time_ + 1);
    ed_time_ = time;
    (*this)[time] = counter;
  }
  void Push(int64_t time, const Counter& counter) {
    if (time <= ed_time_) {
      PushFront(time, counter);
      return; 
    }
    if (time > ed_time_ + space()) {
      PopFront(time - ed_time_ - space());
    }
    if (time > ed_time_ + 1) {
      Counter blank;
      for (auto t = ed_time_ + 1; t < time; ++t)
        PushBack(t, blank);
    }
    PushBack(time, counter);
  }
  void Merge(const TTLQueue& queue) {
    assert(ed_time_ >= queue.ed_time_);
    for (auto t = st_time_ - 1; t > queue.ed_time_; t--) {
      assert(t + 1 == st_time_);
      Counter blank;
      PushFront(t, blank);
    }
    for (auto t = queue.ed_time_; t >= queue.st_time_; --t) {
      if (t >= st_time_)
        (*this)[t] += queue[t];
      else if (!isFull()) {
        assert(t + 1 == st_time_);
        PushFront(t, queue[t]);
      }
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
  void Clear() {
    st_time_ = ed_time_ = create_time_;
    operator[](ed_time_).Clear();
  }
  virtual void GetStringSnapshot(std::vector<KVPair>& snapshot) const override {
    for (auto t = st_time_; t <= ed_time_; ++t) {
      snapshot.emplace_back("Q["+std::to_string(t)+"]", "|");
      operator[](t).GetStringSnapshot(snapshot);
    }
  }
};
struct Statistics : virtual public Statistable, virtual public Printable {
 private:
  std::shared_ptr<StatisticsOptions> options_;
  TTLQueue queue_;
  Counter history_;
 public:
 // a null statistics. neve use it.
  Statistics(std::shared_ptr<StatisticsOptions> options) : options_(nullptr), queue_(0, 0), history_() { assert(options == nullptr); }
 // 
  Statistics(std::shared_ptr<StatisticsOptions> options, TypeTime time) 
  : options_(options), 
    queue_(options_->TimeSliceMaximumSize(), time),
    history_() {}
  Statistics(const Statistics& src) = default;
  virtual void CopyStatistics(std::shared_ptr<Statistable> target) override {
    queue_.Clear();
    history_.Clear();
    MergeStatistics(target);
  }
  virtual void UpdateTime(TypeTime time) override {
    if (queue_.ed_time_ == time) return;
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
      if (queue_.TimeLegal(time)) 
        return queue_[time][label];
      else
        return 0;
    }
  }
  virtual void MergeStatistics(std::shared_ptr<Statistable> target) override {
    if (target == nullptr) return;
    auto stats = std::dynamic_pointer_cast<Statistics>(target);
    MergeStatistics(*stats);
  }
  virtual void ScaleStatistics(TypeLabel label, int numerator, int denominator) override {
    if (label == DefaultCounterTypeMax) {
      for (auto t = queue_.st_time_; t <= queue_.ed_time_; ++t) {
        queue_[t] *= numerator;
        queue_[t] /= denominator;
      }
      history_ *= numerator;
      history_ /= denominator;
    } else {
      for (auto t = queue_.st_time_; t <= queue_.ed_time_; ++t)
        queue_[t].Scale(label, numerator, denominator);
      history_.Scale(label, numerator, denominator);
    }
  }
  virtual ~Statistics() {}

  virtual void GetStringSnapshot(std::vector<KVPair>& snapshot) const override {
    //queue_.GetStringSnapshot(snapshot);
    //snapshot.emplace_back("History", "|");
    history_.GetStringSnapshot(snapshot);
  }
 private:
  virtual void MergeStatistics(const Statistics& target) {
    if (!target.options_) return;
    if (target.queue_.ed_time_ > queue_.ed_time_)
      UpdateTime(target.queue_.ed_time_);
    if (target.queue_.ed_time_ + options_->TimeSliceMaximumSize() > queue_.ed_time_)
      queue_.Merge(target.queue_);
    history_ += target.history_;
  }
 };

}