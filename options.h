#pragma once

#include <vector>
#include <stack>
#include <iostream>
#include "leveldb/env.h"

namespace sagitrs {

#define STATISTICS_PREVIOUS  1
#define STATISTICS_CURRENT   0
#define STATISTICS_ALL      -1
#define STATISTICS_AVERAGE  -2

enum DefaultTypeLabel : uint32_t {
  LeafCount = 0,
  // to record if this node is a leaf node.
  // Construct: Set to 1 for leaf node, 0 for non-leaf node.
  // Update: Never change.
  KSGetCount,
  // to record get operation in key space.
  // Inherited at Compaction.
  // Construct: Set to 0 for all nodee.
  // Update: increase when buffer element is read.    
  ValueGetCount,
  // to record get operation of this value.
  // clear at Compaction.
  KSIterateCount,
  // to record iterate operation of this value.
  // clear at Compaction.
  KSPutCount,
  // to record get operation in key space.
  // Inherited at Compaction.
  // Construct: Set to 0 for all nodee.
  // Update: increase when buffer element is read.   
  PutCount,
  // for special usage.
  // recording total put operations globally.
  DefaultCounterTypeMax,
};

struct SBSNodeOptions {
  // The width of each layer of the node, except for the Head node, 
  // must not be LOWER than this value.
  virtual size_t MinWidth() const = 0;
  // The width of each layer of the node, except for the Head node,
  // must not be HIGHER than this value.
  virtual size_t MaxWidth() const = 0;
  // The default width of the current node after splitting.
  virtual size_t DefaultWidth() const = 0;

  virtual double NeedsCompactionScore() const = 0;
  virtual size_t MaxCompactionFiles() const = 0;

  // Used to detect if a width is below or above the boundary value.
  // Exceptionally, the left border of the head node is not checked.
  int TestState(size_t size, bool lbound_ignore) const {
    if (!lbound_ignore && size < MinWidth()) return -1;
    if (size > MaxWidth()) return 1;
    return 0;
  }
};

struct StatisticsOptions {
  virtual size_t StatisticsLabelMax() const = 0;
  // The duration of a time slice in microseconds.
  // For example, if you want to set the time slice to 60 seconds, 
  // this value would be 60 * 1000000.
  virtual uint64_t TimeSliceMicroSecond() const = 0;
  // The maximum number of time slices that can be recorded.
  virtual uint64_t TimeSliceMaximumSize() const = 0;

  // Waiting time before merging records.
  // -1 means never merge.
  virtual int64_t TimeBeforeMerge() const = 0;

  // Env::NowMicros() is needed to get the current time 
  // to determine which time slice the data is saved in.
  virtual uint64_t NowTimeSlice() const = 0;

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
  static const size_t BaseWidth = 11;
  size_t width_[3] = {BaseWidth * 2 / 3, BaseWidth * 4 / 3};
 public:
  size_t MinWidth() const override { return width_[0]; }
  size_t DefaultWidth() const override { return width_[0]; }
  size_t MaxWidth() const override { return width_[1]; }

// Statistics args: 
 private:
  size_t time_slice_ = 5 * 1000 * 1000;
  size_t time_count_ = 10;
  size_t time_slice_before_merge_ = 2;
  double B = 0.1, I = 0.9, D = 0.0;
  static leveldb::Env* TimerEnv() { return leveldb::Env::Default(); }
 public:
  virtual size_t StatisticsLabelMax() const override { return DefaultCounterTypeMax; }
  virtual uint64_t TimeSliceMicroSecond() const override { return time_slice_; }
  virtual uint64_t TimeSliceMaximumSize() const override { return time_count_; }
  virtual int64_t TimeBeforeMerge() const override { return time_slice_ / 6; }
  virtual uint64_t NowTimeSlice() const override { return TimerEnv()->NowMicros() / time_slice_; }
  virtual double kBaseWeight() const override { return B; }
  virtual double kIntegrationWeight() const override { return I; }
  virtual double kDifferentiationWeight() const override { return D; }

  double needs_compaction_score_ = 1;
  size_t max_compaction_files_ = 15;
  bool force_compaction_ = 0;
  virtual double NeedsCompactionScore() const override { 
    return force_compaction_ ? 0 :needs_compaction_score_; 
  }
  virtual size_t MaxCompactionFiles() const override { return max_compaction_files_; }

 public:
  //size_t max_file_size_ = 2 * 1024 * 1024;
  size_t MaxWriteBufferSize() const { return 16 * 1024 * 1024; }
  size_t MaxFileSize() const { return 2 * 1024 * 1024; }
  size_t Width() const { return BaseWidth; }

 public:
  SBSOptions() = default;
  SBSOptions(const SBSOptions& options) = default;
};

struct DelineatorOptions : public StatisticsOptions {
  virtual uint64_t TimeSliceMicroSecond() const override { return 60 * 1000 * 1000; }
  virtual uint64_t TimeSliceMaximumSize() const override { return 25; }

  // Waiting time before merging records.
  virtual int64_t TimeBeforeMerge() const override { return -1; }
  static leveldb::Env* TimerEnv() { return leveldb::Env::Default(); }
  virtual uint64_t NowTimeSlice() const override { return TimerEnv()->NowMicros() / TimeSliceMicroSecond(); }

  virtual double kBaseWeight() const override { return 0; }
  virtual double kIntegrationWeight() const override { return 0; }
  virtual double kDifferentiationWeight() const override { return 0; }
};


}