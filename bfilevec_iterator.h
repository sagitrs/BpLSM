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
    Iterator* OpenIterator(const leveldb::ReadOptions& roptions, leveldb::Version* v) { 
      assert(!iter_);
      iter_ = v->GetBFileIterator(roptions, file_->Data()->number, file_->Data()->file_size);
      return iter_;
    }
    uint64_t ID() const { return file_->Identifier(); }
    bool isOpened() const { return iter_ != nullptr; }
    //bool Less(const Slice& key) { return file_->Max().compare(key) < 0; }
  };
  
  leveldb::ReadOptions roptions_;
  leveldb::Version* version_;
  std::unordered_map<uint64_t, Handle*> handles_;
  std::vector<uint64_t> forward_;
  int forward_curr_;
  
  BFileVecIterator(const leveldb::Comparator* comparator,
                   const leveldb::ReadOptions& options, 
                   leveldb::Version* version,
                   const std::vector<BFile*>& files) : 
   BaseIter(comparator),
   roptions_(options), version_(version), 
   handles_(), forward_(),
   forward_curr_(-1) { UpdateHandles(files); }
  ~BFileVecIterator() {
    for (auto& p : handles_) {
      assert(p.first == p.second->ID());
      Del(p.first, false);
      delete p.second;
      p.second = nullptr;
    }
  }
 private:
  Handle* F(size_t k) { 
    assert(k < forward_.size());
    auto p = handles_.find(forward_[k]);
    assert(p != handles_.end());
    return p->second; 
  }
  Slice FMin(size_t k) { return F(k)->file_->Min(); }
  Slice FMax(size_t k) { return F(k)->file_->Max(); }
  void Open(Handle* handle, bool echo = true) {
    assert(!handle->isOpened());
    BaseIter::Add(handle->ID(), handle->OpenIterator(roptions_, version_), echo);
  }
  size_t N() const { return handles_.size(); }
 public:
  void UpdateHandles(const std::vector<BFile*>& files) {
    std::set<uint64_t> reserved_id;
    std::set<uint64_t> removed_id;
    for (BFile* file : files) 
      reserved_id.insert(file->Identifier());
    for (auto& p : handles_) 
      if (reserved_id.find(p.first) == reserved_id.end())
        removed_id.insert(p.first);
      else
        reserved_id.erase(p.first);
    std::set<uint64_t>& added_id = reserved_id;
    for (uint64_t id : removed_id)
      handles_.erase(id);
    for (BFile* file : files) 
      if (added_id.find(file->Identifier()) != added_id.end())
        handles_.emplace(file->Identifier(), new Handle(file));

    forward_.clear();
    for (auto& p : handles_)
      forward_.push_back(p.first);
    for (size_t i = 0; i+1 < handles_.size(); ++i) 
      for (size_t j = 0; j+i+1 < handles_.size(); ++j) {
        size_t k = j+1;
        Slice fj(FMin(j)), fk(FMin(k));
        if (fj.compare(fk) > 0) 
          std::swap(forward_[j], forward_[k]);
      }
  }
  bool SubValid(uint64_t id) {
    auto p = iters_.find(id);
    if (p == iters_.end()) {
      Slice user_key(Valid() ? 
        leveldb::ExtractUserKey(key()) : 
        istat_.key_);
      auto r = handles_.find(id);
      assert(r != handles_.end());
      return user_key.compare(r->second->file_->Max()) <= 0;
    } else {
      return p->second.Valid();
    }
  }
  
  virtual void SeekToFirst() override { 
    if (N() == 0) return;
    leveldb::Slice head(FMin(0));
    for (forward_curr_ = 0; 
         forward_curr_ < N() && 
         FMin(forward_curr_).compare(head) <= 0; 
         ++forward_curr_)
      if (!F(forward_curr_)->isOpened())
        Open(F(forward_curr_), false);
    BaseIter::SeekToFirst();
  }
  virtual void SeekToLast() override {
    // unfinished.
    assert(false);
  }
  size_t OpenEqual(size_t k, bool echo) {
    size_t opened = 0;
    std::string key = FMin(k).ToString();
    size_t tail;
    for (tail = forward_curr_ + 1; tail < forward_.size(); ++tail) {
      int cmp = FMin(tail).compare(key);
      if (cmp == 0) continue;
      if (cmp < 0) assert(false);
      break;
    }
    for (size_t i = forward_curr_; i < tail; ++i) {
      if (!F(i)->isOpened())
        Open(F(i), echo);
      opened ++;
    }
    return opened;
  }
  size_t OpenFirst(const Slice& lbound, const Slice& rbound, bool echo) {
    //assert(forward_curr_ < forward_.size());
    // no file covers this key, but a result may be needed.
    size_t opened = 0;
    for (size_t i = forward_curr_; i < forward_.size(); ++i) {
      bool lfit = FMin(i).compare(lbound) >= 0;
      bool rfit = FMin(i).compare(rbound) <= 0;
      assert(lfit);
      if (!rfit) {
        // this file shouldn't be open now.
        return opened;
      }
      opened += OpenEqual(i, echo);
      if (opened > 0) return opened;
    }
    return opened;
  }
  void LocateForward(const Slice& key) {
    for (; forward_curr_ < forward_.size(); ++forward_curr_)
      if (FMin(forward_curr_).compare(key) >= 0)
        break;
  }
  virtual void Seek(const Slice& ikey) override {
    // F(forward_curr_) > key >= F(forward_curr_ - 1).
    // 1. find if key in files <= key.
    // 2. if no file contains this key,  open mininum file > key.
    Slice key(leveldb::ExtractUserKey(ikey));
    forward_curr_ = 0;
    LocateForward(key);
    size_t opened = 0;
    for (size_t i = 0; i < forward_curr_; ++i) {
      if (FMin(i).compare(key) <= 0 && FMax(i).compare(key) >= 0) {
        if (!F(i)->isOpened())
          Open(F(i), false);
        opened ++;
      }
    }
    BaseIter::Seek(key);
    if (Valid()) {
      return;
    }
    assert(opened == 0);
    // no file covers this key. Any file larger than this key?
    if (forward_curr_ == forward_.size()) {
      return;
      // no key in this iterator equal or larger than ikey.
    }
    opened = OpenEqual(forward_curr_, false);
    if (opened > 0) {
      BaseIter::Seek(key);
      assert(Valid()); 
    } else {
      assert(false);
    } 
  }
  virtual void Next() override {
    // 1. Seek from key1 to key2.
    // 2. Open files between (key1, key2], reopen.
    // 3. if seek failed, open next files.
    
    assert(Valid());
    BaseIter::Next();
    Slice key1(leveldb::ExtractUserKey(istat_.key_));
    size_t opened = 0;
    LocateForward(key1);
    if (!Valid()) {
      // current file is finished.
      opened = OpenEqual(forward_curr_, true);
    } else {
      Slice key2(leveldb::ExtractUserKey(key()));
      opened = OpenFirst(key1, key2, true);
    }
    if (!Valid()) {
      assert(opened == 0);
      // reach the end. 
    } else {
    }
  }
  virtual void Prev() override {
    assert(false);
    // unfinished.
  }
  
};

}  // namespace leveldb
