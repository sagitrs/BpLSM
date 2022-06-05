#pragma once
#include "db/version_set.h"
#include "leveldb/comparator.h"
#include "leveldb/iterator.h"
#include "leveldb/slice.h"
#include "leveldb/options.h"
#include "table/merger.h"

#include "bfile.h"
#include "third_party/sb-skiplist/sbs_iterator.h"
#include "../../table/dynamic_merger.h"

typedef leveldb::DynamicMergingIterator BaseIter;

namespace sagitrs {

struct BFileVecIterator : public BaseIter {
  struct Handle {
    BFile* file_;
    Iterator* iter_;
    Handle(BFile* file) : file_(file), iter_(nullptr) {}
    void OpenIterator(const leveldb::ReadOptions& roptions, leveldb::Version* v) { 
      assert(!iter_);
      iter_ = v->GetBFileIterator(roptions, file_->Data()->number, file_->Data()->file_size);
    }
    bool isOpened() const { return iter_ != nullptr; }
  };
  
  leveldb::ReadOptions roptions_;
  leveldb::Version* version_;
  std::vector<Handle> handles_;
  std::vector<Handle*> forward_, backward_;
  int forward_curr_, backward_curr_;
  
  BFileVecIterator(const leveldb::Comparator* comparator,
                   const leveldb::ReadOptions& options, 
                   leveldb::Version* version,
                   const std::vector<BFile*>& files) : 
   BaseIter(comparator),
   roptions_(options), version_(version), 
   handles_(), forward_(), backward_(),
   forward_curr_(-1), backward_curr_(-1) {
    for (BFile* file : files) 
      handles_.emplace_back(file);
    for (size_t i = 0; i < files.size(); ++i) {
      forward_.push_back(&handles_[i]);
      backward_.push_back(&handles_[i]);
    }
    std::sort(forward_.begin(), forward_.end(), [](Handle* a, Handle* b) {
      return a->file_->Min().compare(b->file_->Min()) < 0;});
    std::sort(backward_.begin(), backward_.end(), [](Handle* a, Handle* b) {
      return a->file_->Max().compare(b->file_->Max()) > 0;});
  }

  void Open(Handle* handle, bool echo = true) {
    assert(!handle->isOpened());
    handle->OpenIterator(roptions_, version_);
    BaseIter::Add(handle->iter_, echo);
  }
  size_t N() const { return handles_.size(); }

  virtual void SeekToFirst() override { 
    if (N() == 0) return;
    leveldb::Slice head(forward_[0]->file_->Min());
    for (forward_curr_ = 0; 
         forward_curr_ < N() && 
         BaseIter::comparator_->Compare(forward_[forward_curr_]->file_->Min(), head) <= 0; 
         ++forward_curr_)
      if (!forward_[forward_curr_]->isOpened())
        Open(forward_[forward_curr_], false);
    BaseIter::SeekToFirst();
  }
  virtual void SeekToLast() override {
    // unfinished.
    assert(false);
  }
  virtual void Seek(const Slice& key) override {
    for (forward_curr_ = 0; 
         forward_curr_ < N() && 
         BaseIter::comparator_->Compare(forward_[forward_curr_]->file_->Min(), key) <= 0 &&
         BaseIter::comparator_->Compare(forward_[forward_curr_]->file_->Max(), key) >= 0; 
         ++forward_curr_)
      if (!forward_[forward_curr_]->isOpened())
        Open(forward_[forward_curr_], false); 
    BaseIter::Seek(key);
  }
  virtual void Next() override {
    BaseIter::Next();
    for (; 
         forward_curr_ < N() && 
         BaseIter::comparator_->Compare(forward_[forward_curr_]->file_->Min(), key()) <= 0 &&
         BaseIter::comparator_->Compare(forward_[forward_curr_]->file_->Max(), key()) >= 0; 
         ++forward_curr_)
      if (!forward_[forward_curr_]->isOpened())
        Open(forward_[forward_curr_], true); 
  }
  virtual void Prev() override {
    assert(false);
    // unfinished.
  }
  
};

}  // namespace leveldb
