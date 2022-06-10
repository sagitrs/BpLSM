#pragma once

#include <atomic>
#include <map>
#include <string>

#include "leveldb/slice.h"
#include "../../db/memtable.h"
using leveldb::Slice;

namespace sagitrs {

struct SamplerTable : public std::map<std::string, int> {
  public:
  SamplerTable() : std::map<std::string, int>() {}
  void Add(const Slice& key, size_t count = 1) {
    std::string skey(key.data(), key.size());
    auto p = find(skey);
    if (p != end())
      p->second += count;
    else 
      (*this)[skey] = count;
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
    p--;
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
  SamplerTable read_sampler_, write_sampler_, write_bytes_sampler_, iterate_sampler_;
  //std::atomic<uint64_t> memory_usage_, record_count_;
 public:
  Sampler() : 
    options_(),
    write_sampler_(), read_sampler_(), write_bytes_sampler_(), iterate_sampler_()
    //memory_usage_(0), record_count_(0)
  {}
  ~Sampler() {}
  void WriteSample(const Slice& key, size_t value_size) {
    write_sampler_.Add(key);
    write_bytes_sampler_.Add(key, key.size() + value_size + 10);
    //global_sampler_.Add(key);
  }
  void ReadSample(const Slice& key) { read_sampler_.Add(key); }
  void IterateSample(const Slice& key) { iterate_sampler_.Add(key); }
  SamplerTable& WriteTable() { return write_sampler_; }
  SamplerTable& ReadTable() { return read_sampler_; }
  SamplerTable& IterateTable() { return iterate_sampler_; }
  SamplerTable& WriteBytesTable() { return write_bytes_sampler_; }
};

}