#pragma once

#include <vector>
#include <stack>
#include <iostream>
#include <cmath>
#include <algorithm>
#include "leveldb/env.h"

#include "sampler.h"

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
  KSBytesCount,
  PutCount,
  // for special usage.
  // recording total put operations globally.
  DefaultCounterTypeMax,
};

struct SBSNodeOptions {
// Width limit of nodes:
 private:
 public:
  static const size_t BaseWidth = 9;
  size_t width_[3] = {BaseWidth * 2 / 3, BaseWidth, BaseWidth * 4 / 3};
  // The width of each layer of the node, except for the Head node, 
  // must not be LOWER than this value.
  size_t MinWidth() const { return width_[0]; }
  // The width of each layer of the node, except for the Head node,
  // must not be HIGHER than this value.
  size_t MaxWidth() const { return width_[2]; }
  // The default width of the current node after splitting.
  size_t DefaultWidth() const { return width_[1]; }

  double needs_compaction_score_ = 1;
  size_t max_compaction_files_ = 64;
  bool force_compaction_ = 0;
  virtual double NeedsCompactionScore() const { 
    return force_compaction_ ? 0 : needs_compaction_score_; 
  }
  virtual size_t MaxCompactionFiles() const { return max_compaction_files_; }

  // Used to detect if a width is below or above the boundary value.
  // Exceptionally, the left border of the head node is not checked.
  int TestState(size_t size, bool lbound_ignore) const {
    if (!lbound_ignore && size < MinWidth()) return -1;
    if (size > MaxWidth()) return 1;
    return 0;
  }
};

struct StatisticsOptions {
 private:
  static leveldb::Env* TimerEnv() { return leveldb::Env::Default(); }
  size_t time_slice_ = 10 * 1000 * 1000;
  size_t time_count_ = 10;
  size_t time_slice_before_merge_ = 2;
  double B = 0.1, I = 0.9, D = 0.0;
 public:
  virtual size_t StatisticsLabelMax() const { return DefaultCounterTypeMax; }
  
  // The duration of a time slice in microseconds.
  // For example, if you want to set the time slice to 60 seconds, 
  // this value would be 60 * 1000000.
  virtual uint64_t TimeSliceMicroSecond() const { return time_slice_; }

  // The maximum number of time slices that can be recorded.
  virtual uint64_t TimeSliceMaximumSize() const { return time_count_; }

  // Waiting time before merging records.
  // -1 means never merge.
  virtual int64_t TimeBeforeMerge() const { return time_slice_ / 6; }

  // Env::NowMicros() is needed to get the current time 
  // to determine which time slice the data is saved in.
  virtual uint64_t NowTimeSlice() const { return TimerEnv()->NowMicros() / time_slice_; }

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
  virtual double kBaseWeight() const { return B; }
  virtual double kIntegrationWeight() const { return I; }

  // It is unclear whether differential computation makes sense, 
  // and I personally recommend setting it to 0, 
  // even if the mechanism associated with it works properly.
  virtual double kDifferentiationWeight() const { return D; }
};

struct CompactionOptions {
  //CompactionOptions(const SBSOptions& options, sagitrs::SamplerTable* table = nullptr)
  //  : table_(table), 
  //    sample_per_file_(options.MaxFileSize() / options.CompactSampleConst() / PageConst),
  //    sample_per_output_file_(sample_per_file_ / OutputFileMinConst),
  //    force_pick_(true) {}
  sagitrs::SamplerTable* table_;
};
struct SBSOptions : public SBSNodeOptions, 
                    public StatisticsOptions,
                    public CompactionOptions {

// Statistics args: 

 public:
  //size_t max_file_size_ = 2 * 1024 * 1024;
  size_t MaxWriteBufferSize() const { return 32 << 20; }
  size_t MaxFileSize() const { return 4 << 20; }
  size_t Width() const { return SBSNodeOptions::BaseWidth; }

  size_t kMaxHeight() const { return 6; }

  double SpaceAmplificationConst() const { return 0.2; }
  double CacheCapacity() const { return 0.3; }
  double ApproximateBufferNodeConst() const { return 1.0 / (DefaultWidth() - 1); }
  //double FilesPerNode() const { return SpaceAmplificationConst() / ApproximateBufferNodeConst(); }
  double LevelCapabilityConst(size_t level) const { return std::pow(1.0 / DefaultWidth(), level); }
 
  inline size_t ReadSampleConst() const { return 50; }
  inline size_t WriteSampleConst() const { return 50; }
  inline size_t IterateSampleConst() const { return 1; }
  inline size_t CompactSampleConst() const { return 10; }
  
  size_t level0_compaction_size_ = 8;
  inline size_t Level0CompactionSize() const { return level0_compaction_size_; }
  inline double SlowDownScore() const { return 1.9; }
  inline double StopScore() const { return 2; }
  
  static const size_t PageConst = 4096;
  size_t SamplePerInputFile() const { return MaxFileSize() / CompactSampleConst() / PageConst; }
  size_t SamplePerOutputFile() const { return SamplePerInputFile() * 9 / 10; }
  size_t OutputFileMinSize() const { return MaxFileSize() * 2 / 3; }
  //size_t sample_per_file_ = max_file / options.CompactSampleConst() / PageConst
  //size_t sample_per_output_file_;
  bool force_pick_ = 1;
  bool ForcePick() const { return force_pick_; }

 public:
  SBSOptions() = default;
  SBSOptions(const SBSOptions& options) = default;
};


}