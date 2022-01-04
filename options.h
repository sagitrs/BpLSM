#pragma once

#include <vector>
#include <stack>
#include "leveldb/env.h"

namespace sagitrs {

struct SBSNodeOptions {
  // The width of each layer of the node, except for the Head node, 
  // must not be LOWER than this value.
  virtual size_t MinWidth() const = 0;
  // The width of each layer of the node, except for the Head node,
  // must not be HIGHER than this value.
  virtual size_t MaxWidth() const = 0;
  // The default width of the current node after splitting.
  virtual size_t DefaultWidth() const = 0;

  // Used to detect if a width is below or above the boundary value.
  // Exceptionally, the left border of the head node is not checked.
  int TestState(size_t size, bool lbound_ignore) const {
    if (!lbound_ignore && size < MinWidth()) return -1;
    if (size > MaxWidth()) return 1;
    return 0;
  }
};

struct StatisticsOptions {
  // The duration of a time slice in microseconds.
  // For example, if you want to set the time slice to 60 seconds, 
  // this value would be 60 * 1000000.
  virtual uint64_t TimeSliceMicroSecond() const = 0;
  // The maximum number of time slices that can be recorded.
  virtual uint64_t TimeSliceMaximumSize() const = 0;

  // Env::NowMicros() is needed to get the current time 
  // to determine which time slice the data is saved in.
  virtual leveldb::Env* TimerEnv() const = 0;

  // We have the following ways to obtain statistics for a time slice.
  // 1. L = getting the most recent complete time slice record.
  // 2. D = getting the difference between the most recent complete 
  // time slice and the previous complete time slice record.
  // 3. I = Get the average of all records in the entire history.
  // The final result will return the weighted average of the above, 
  // and the user can specify the weights.
  
  // For example, if L=0.1, I=0.9 and D=0, the returned value will 
  // be closer to the average of all history records.

  // It is recommended that users ensure that the sum of these two 
  // values is 1. If not, we will normalize them. 
  virtual double kBaseWeight() const = 0;  
  virtual double kIntegrationWeight() const = 0;

  // It is unclear whether differential computation makes sense, 
  // and I personally recommend setting it to 0, 
  // even if the mechanism associated with it works properly.
  virtual double kDifferentiationWeight() const = 0;
};

struct SBSOptions : public SBSNodeOptions, public StatisticsOptions {
// Width limit of nodes:
 private:
  size_t width_[3] = {2, 4, 8};
 public:
  size_t MinWidth() const override { return width_[0]; }
  size_t DefaultWidth() const override { return width_[1]; }
  size_t MaxWidth() const override { return width_[2]; }

// Statistics args: 
 private:
  size_t time_slice_ = 60 * 1000 * 1000;
  size_t time_count_ = 10;
  double B = 0.1, I = 0.9, D = 0.0;
 public:
  uint64_t TimeSliceMicroSecond() const override { return time_slice_; }
  uint64_t TimeSliceMaximumSize() const override { return time_count_; }
  virtual leveldb::Env* TimerEnv() const override { return leveldb::Env::Default(); }
  double kBaseWeight() const override { return B; }
  double kIntegrationWeight() const override { return I; }
  double kDifferentiationWeight() const override { return D; }

// User-defined parameters:
 private:
  size_t level0_buffer_size_ = 8;
  size_t global_buffer_size_ = 1;
 public:
  size_t RootBufferLimit() { return level0_buffer_size_; }
  size_t GlobalBufferLimit() { return global_buffer_size_; }
};


}