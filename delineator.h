#pragma once

#include "bounded.h"


namespace sagitrs {

struct Delineator {
 private:
  struct RangedStatistics {
    std::string guard_;
    std::shared_ptr<Statistable> stats_;
   public:
    RangedStatistics(const Slice& guard, std::shared_ptr<Statistable> stats) : guard_(guard.ToString()), stats_(stats) {}
  };
  std::vector<RangedStatistics> vec_;
 public:
  Delineator() : vec_() {}
  virtual ~Delineator() {}
  void AddStatistics(const Slice& guard, std::shared_ptr<Statistable> stats) { 
    vec_.emplace_back(guard, stats); 
  }
  int64_t GetStatistics(size_t k, uint32_t label, int64_t time) { 
    return vec_[k].stats_ ? vec_[k].stats_->GetStatistics(label, time) : 0; 
  }
  void GetAllStatistics(std::vector<int64_t>& data, uint32_t label, size_t time) {
    for (size_t i = 0; i < vec_.size(); ++i)
      data.push_back(GetStatistics(i, label, time));
  }
  void OldPrintTo(std::ostream& os, int64_t time, DefaultTypeLabel label, size_t height = 10, size_t width = 60) {
    size_t n = vec_.size();
    std::vector<bool> mask(n, 1);
    BuildMask(mask, width);

    for (int64_t t = time - height; t <= time; ++t) {
      std::vector<int64_t> data;
      GetAllStatistics(data, label, t);
      if (data.size() != 0) {
        int64_t lbound = data[0], rbound = data[0];
        for (const auto& a : data) {
          if (a < lbound) lbound = a;
          if (a > rbound) rbound = a;
        }
        os << "|";
        for (size_t i = 0; i < n; ++i) if (mask[i])
          os << Graphical(data[i], lbound, rbound);
        os << "(" << (rbound - lbound) << "/" <<  lbound << ")";
      } 
      os << std::endl;
    }
    os << "+"; for (size_t i = 1; i < width; ++i) os << "-"; os << std::endl;
    size_t max_paces_size = 0;

    for (size_t i = 0; i < n; ++i) 
      if (mask[i] && vec_[i].guard_.size() > max_paces_size)
        max_paces_size = vec_[i].guard_.size();
    for (size_t i = 0; i < max_paces_size; ++i) {
      os << "#";
      for (size_t j = 0; j < n; ++j) if (mask[j])
        os << (i < vec_[j].guard_.size() ? vec_[j].guard_[i] : ' ');
      os << std::endl;
    }
  }
 
  void PrintTo(std::ostream& os, int64_t time, DefaultTypeLabel label, size_t height = 10, size_t width = 20) {
    size_t n = vec_.size();
    std::vector<bool> mask(n, 1);
    BuildMask(mask, width);

    for (int64_t t = time - height; t <= time; ++t) {
      std::vector<int64_t> data;
      GetAllStatistics(data, label, t);
      if (data.size() != 0) {
        int64_t lbound = 0, rbound = 0;
        std::vector<int64_t> merged(width, 0);
        size_t step = n / width;
        for (size_t i = 0; i < width; ++i) {
          for (size_t j = 0; j < step; ++j) {
            size_t k = i * step + j;
            if (k >= n) {
              merged[i] = merged[i] * j / step;
              break;
            }
            merged[i] += data[k];
          }
          if (i == 0) lbound = rbound = merged[0];
          else { 
            if (merged[i] < lbound) lbound = merged[i];
            if (merged[i] > rbound) rbound = merged[i];
          }
        }
        os << "|";
        for (size_t i = 0; i < width; ++i)
          os << Graphical(merged[i], lbound, rbound);
        os << "(" << (rbound - lbound) << "/" <<  lbound << ")";
      } 
      os << std::endl;
    }
    os << "+"; for (size_t i = 1; i < width; ++i) os << "-"; os << std::endl;
    size_t max_paces_size = 0;

    for (size_t i = 0; i < n; ++i) 
      if (mask[i] && vec_[i].guard_.size() > max_paces_size)
        max_paces_size = vec_[i].guard_.size();
    for (size_t i = 0; i < max_paces_size; ++i) {
      os << "#";
      for (size_t j = 0; j < n; ++j) if (mask[j])
        os << (i < vec_[j].guard_.size() ? vec_[j].guard_[i] : ' ');
      os << std::endl;
    }
  }
 private:
  void BuildMask(std::vector<bool> &mask, size_t n) {
    size_t total = mask.size();
    if (total <= n) return;

    for (size_t i = 0; i < total; ++i) mask[i] = 0;
    size_t step = total / n + (total % n > 0);
    for (size_t i = 0; i < total; i += step)
      mask[i] = 1;
  }
  char Graphical(int64_t value, int min, int max) {
    char graph[] = {'-', '1', '2', '3', '4', '5', '6', '7', '8', '9', '@', '@', '@'};
    int64_t x = value - min, step = (max - min) / 10;
    if (step == 0) return graph[0];
    return graph[x / step];
  }
};

}
