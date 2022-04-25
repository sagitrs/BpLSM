#pragma once

#include <sstream>
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

struct Printable {
  typedef std::pair<std::string, std::string> KVPair;
  virtual void GetStringSnapshot(std::vector<KVPair>& snapshot) const = 0;
  virtual void PrintTo(std::ostream& os) const {
    std::vector<KVPair> snapshot;
    GetStringSnapshot(snapshot);
    for (auto& kv : snapshot) { os << KVPairToString(kv) << ","; }
  }
  virtual std::string ToString() const {
    std::stringstream ss;
    PrintTo(ss);
    return ss.str();
  }
  static std::string KVPairToString(const KVPair& kv, int width = -1) {
    std::string res = kv.first + ":" + kv.second;
    if (width == -1) return res;
    if (res.size() >= width) return res.substr(0, width);
    std::string suffix(width - res.size(), ' ');
    return res + suffix;
  } 
};

struct Statistable {
  typedef uint32_t TypeLabel;
  typedef int64_t TypeData;
  typedef int64_t TypeTime;

  virtual void UpdateTime(TypeTime time) = 0;
  
  virtual void UpdateStatistics(TypeLabel label, TypeData diff, TypeTime time) = 0;
  virtual TypeData GetStatistics(TypeLabel type, TypeTime time) = 0;
  virtual void MergeStatistics(std::shared_ptr<Statistable> target) = 0;  
  virtual void CopyStatistics(std::shared_ptr<Statistable> target) = 0;
  virtual void ScaleStatistics(TypeLabel label, int numerator, int denominator) = 0;
  
  virtual ~Statistable() {}
};

struct Identifiable {
  virtual uint64_t Identifier() const = 0;
  virtual uint64_t Size() const = 0;
  virtual void* Value() const = 0;
  bool operator ==(const Identifiable& obj) const { return Identifier() == obj.Identifier(); }
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

struct BoundedValue : virtual public Bounded, 
                      virtual public Identifiable, 
                      virtual public Statistable 
{
  
}; 

struct RealBounded : virtual public Bounded {
 private:
  std::string min_, max_;
 public:
  RealBounded(const Slice& min, const Slice& max) : min_(min.ToString()), max_(max.ToString()) {}
  virtual ~RealBounded() {}
  virtual Slice Min() const override { return min_; }
  virtual Slice Max() const override { return max_; }
 public: 
  void Extend(const Slice& a, const Slice& b) {
    if (a.compare(min_) < 0) min_ = a.ToString();
    if (b.compare(max_) > 0) max_ = b.ToString();
  }
  void Extend(const Bounded& target) { Extend(target.Min(), target.Max()); }
  virtual void Rebound(const Slice& a, const Slice& b) {
    min_ = a.ToString();
    max_ = b.ToString();
  }
  virtual void Rebound(const Bounded& target) { Rebound(target.Min(), target.Max()); }
  bool OnBound(const Slice& a, const Slice& b) { return (a.compare(min_) == 0 || b.compare(max_) == 0); }
  bool OnBound(const Bounded& target) { return OnBound(target.Min(), target.Max()); }
};

}
