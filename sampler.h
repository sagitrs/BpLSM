#pragma once

#include <atomic>
#include <map>
#include <string>

#include "../../db/memtable.h"

namespace sagitrs {

struct SamplerTable : public std::map<std::string, int> {
  public:
  SamplerTable() : std::map<std::string, int>() {}
  void Add(const Slice& key) {
    std::string skey(key.data(), key.size());
    auto p = find(skey);
    if (p != end())
      p->second ++;
    else 
      (*this)[skey] = 1;
  }
  void StopSampling() {
    size_t total = 0;
    for (auto iter = begin(); iter != end(); ++iter) {
      total += iter->second;
      iter->second = total;
    }
  }
  size_t GetCountSmallerOrEqualThan(const Slice& key) {
    std::string skey(key.data(), key.size());
    auto p = find(skey);
    if (p != end()) 
      return p->second;

    (*this)[skey] = 0;
    p = find(skey);
    assert(p != end());
    if (p == begin())
      return 0;
    (*this)[skey] = p->second;
    return p->second;
  }
};

struct Sampler {
 private:
  struct SamplerOptions {
    size_t sample_rate = 100;
  };
  
  SamplerOptions options_;
  SamplerTable read_sampler_, write_sampler_, global_sampler_;
  std::atomic<uint64_t> memory_usage_, record_size_;
 public:
  Sampler() : 
    options_(),
    write_sampler_(), read_sampler_(),
    memory_usage_(0), record_size_(500)
  {}
  ~Sampler() {}
  void WriteSample(const Slice& key, size_t value_size) {
    memory_usage_ += key.size() + value_size;
    write_sampler_.Add(key);
    global_sampler_.Add(key);
  }
  void ReadSample(const Slice& key) { read_sampler_.Add(key); }
  SamplerTable& WriteTable() { return write_sampler_; }
  SamplerTable& ReadTable() { return read_sampler_; }
  SamplerTable& GlobalTable() { return global_sampler_; }
  void GlobalClear() {
    uint64_t l = record_size_.load(std::memory_order_relaxed);
    record_size_.store((l + RecordSize()) / 2, std::memory_order_relaxed);
    memory_usage_ = 0;
    global_sampler_.clear();
  }
  double RecordSize() const { 
    return 1.0 * memory_usage_.load(std::memory_order_relaxed) / global_sampler_.size(); 
  }
};

}