#pragma once
#include "statistics.h"

namespace sagitrs {

struct Delineator : private Statistics {
 public:
  Delineator(std::shared_ptr<StatisticsOptions> options) : Statistics(options) {}
  virtual ~Delineator() {}

  using Statistics::Inc;
  void SetPacesetter(std::vector<std::string>& paces) {
    for (auto& key : paces)
      InsertBlankShard(key);
  }
  using Statistics::Superposition;

  std::string ToString(DefaultCounterType label = GetCount, size_t width = 60) {
    std::vector<std::string> paces;
    GetPacesetter(paces);
    size_t n = paces.size();
    std::vector<bool> mask(n, 1);
    BuildMask(mask, width);
    std::stringstream ss;

    for (int64_t time = options_->TimeSliceMaximumSize() - 5; time >=0; --time) {
      std::vector<int64_t> data;
      Get(data, label, time);
      if (data.size() != 0) {
        int64_t lbound = data[0], rbound = data[0];
        for (const auto& a : data) {
          if (a < lbound) lbound = a;
          if (a > rbound) rbound = a;
        }
        ss << "|";
        for (size_t i = 0; i < n; ++i) if (mask[i])
          ss << Graphical(data[i], lbound, rbound);
      }
      ss << std::endl;
    }
    ss << "+"; for (size_t i = 1; i < width; ++i) ss << "-"; ss << std::endl;
    size_t max_paces_size = 0;
    for (size_t i = 0; i < n; ++i) 
      if (mask[i] && paces[i].size() > max_paces_size)
        max_paces_size = paces[i].size();
    for (size_t i = 0; i < max_paces_size; ++i) {
      ss << "#";
      for (size_t j = 0; j < n; ++j) if (mask[j])
        ss << (i < paces[j].size() ? paces[j][i] : ' ');
      ss << std::endl;
    }
    return ss.str();
  }
 private:
  void GetPacesetter(std::vector<std::string>& paces) {
    for (auto shard : shards_)
      paces.push_back(shard->Guard().ToString());
  }
  void Get(std::vector<int64_t>& data, DefaultCounterType label, size_t time_before) {
    for (auto shard : shards_)
      data.push_back(shard->Get(TypeSpecified, label, time_before));
  }
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