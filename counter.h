#pragma once

#include <array>

namespace sagitrs {

enum DefaultCounterType : uint32_t {
  RangeSeekCount = 0,
  PointSeekCount,
  PutCount,
  QueryHitCount,
  QueryCacheHitCount,
  DefaultCounterTypeMax
};

struct Counter {
  typedef uint32_t TypeLabel;
  typedef size_t TypeData;
 private:
  std::array<TypeData, DefaultCounterTypeMax> list_;
 public:
  TypeData operator[](size_t k) const { return list_[k]; }
  virtual void Inc(TypeLabel label, TypeData size) { list_[label] += size; }
  virtual void Dec(TypeLabel label, TypeData size) { list_[label] -= size; } 
  virtual void Scale(double k) {
    for (auto& element : list_)
      element *= k; 
  }
  virtual void Superposition(const Counter& target) {
    for (size_t i = 0; i < DefaultCounterTypeMax; ++i)
      list_[i] += target[i];
  }
};

}