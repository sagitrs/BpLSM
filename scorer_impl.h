#pragma once

#include <vector>
#include <stack>
#include <algorithm>
#include <math.h>
#include "sbs_node.h"
#include "sbs_iterator.h"
#include "delineator.h"

namespace sagitrs {

struct Scorer;

struct LeveledScorer : public Scorer {
 private:
  size_t base_children_ = 10;
  const size_t allow_seek_ = (uint64_t)(4) * 1024 * 1024 / 16384U;
 public:
  LeveledScorer() {}
  using Scorer::Init;
  using Scorer::Reset;
  using Scorer::Update;
  using Scorer::MaxScore;
  using Scorer::GetScore;
  using Scorer::ValueScore;
 private: // override this function.
  virtual double ValueCalculate(std::shared_ptr<BoundedValue> value) override { 
    return std::max(0.1, 1.0 * value->GetStatistics(ValueGetCount, STATISTICS_ALL) / allow_seek_); 
  }
};

}
