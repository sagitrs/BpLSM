#pragma once

#include <vector>
#include <string>
#include <mutex>
#include "leveldb/slice.h"

using leveldb::Slice;

namespace sagitrs {

enum BCP : uint8_t {
  BLess, BGreater, BOverlap,   // Compare() will return one of them.
  BInclude, BSubset, BExclude, // Cover()/Include()/In() will return one of them. 
  BEqual, BDifferent           // will not be used.
};

struct Value {
  virtual ~Value() {}
  virtual uint64_t Identifier() const { return reinterpret_cast<uint64_t>(this); }
  bool operator ==(const Value& obj) const { return Identifier() == obj.Identifier(); }
  virtual std::string ToString() const { return std::to_string(Identifier()); }
};

struct Bounded {
 public: // Destructor.
  virtual ~Bounded() {}
 
 public : // Has range.
  virtual Slice Min() const = 0;
  virtual Slice Max() const = 0;

 public: // Compare.
  // Return 0 when and only when the key is between min_key and max_key.
  // Return 1 if key is less than min_key return -1 if key is greater than max_key.
  virtual BCP Include(const Slice& key) const {
    return Min().compare(key) <= 0 && key.compare(Max()) <= 0 ? BInclude : BExclude;
  }
  virtual BCP Compare(const Bounded& node) const {
    if (Max().compare(node.Min()) < 0) return BLess;
    if (Min().compare(node.Max()) > 0) return BGreater;
    return BOverlap;
  }
  virtual BCP Include(const Bounded& node) const {
    if (Compare(node) != BOverlap) return BExclude;
    int lcmp = Min().compare(node.Min());
    int rcmp = Max().compare(node.Max());
    if (lcmp <= 0 && rcmp >= 0) return BInclude;
    return BExclude;
  }
  bool operator < (const Bounded& target) const {
    return Min().compare(target.Min()) < 0;
  }
};

struct BoundedValue : virtual public Value, virtual public Bounded {}; 

struct BRealBounded : virtual public Bounded {
 private:
  std::string min_, max_;
 public:
  BRealBounded(const Slice& min, const Slice& max) : min_(min.ToString()), max_(max.ToString()) {}
  virtual ~BRealBounded() {}
  virtual Slice Min() const override { return min_; }
  virtual Slice Max() const override { return max_; }
 public: 
  virtual void Extend(const Slice& a, const Slice& b) {
    if (a.compare(min_) < 0) min_ = a.ToString();
    if (b.compare(max_) > 0) max_ = b.ToString();
  }
  virtual void Extend(const Bounded& target) { Extend(target.Min(), target.Max()); }
  virtual void Rebound(const Slice& a, const Slice& b) {
    min_ = a.ToString();
    max_ = b.ToString();
  }
  virtual void Rebound(const Bounded& target) { Rebound(target.Min(), target.Max()); }
  bool OnBound(const Slice& a, const Slice& b) { return (a.compare(min_) == 0 || b.compare(max_) == 0); }
  bool OnBound(const Bounded& target) { return OnBound(target.Min(), target.Max()); }
};

struct FakeBoundedValue : public BRealBounded, virtual public Value {
  FakeBoundedValue(const Slice& min, const Slice& max)
  : BRealBounded(min, max) {} 
};

}
