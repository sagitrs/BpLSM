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
enum CursorState { CursorNotFound, LessThanCursor, GreaterThanCursor, OverlapsCursor };
struct BFileVecIterator : public BaseIter {
  struct Handle {
    BFile* file_;
    Iterator* iter_;
    CursorState cs_;
    Handle(BFile* file) : file_(file), iter_(nullptr), cs_(CursorNotFound) {}
    Iterator* OpenIterator(const leveldb::ReadOptions& roptions, leveldb::Version* v) { 
      assert(!iter_);
      iter_ = v->GetBFileIterator(roptions, file_->Data()->number, file_->Data()->file_size);
      return iter_;
    }
    uint64_t ID() const { return file_->Identifier(); }
    bool isOpened() const { return iter_ != nullptr; }
    CursorState State() const { return cs_; }
    CursorState UpdateCursor(const Slice& cursor) {
      if (cursor.compare(file_->Min()) < 0)
        cs_ = GreaterThanCursor;
      else if (cursor.compare(file_->Max()) > 0)  
        cs_ = LessThanCursor;
      else
        cs_ = OverlapsCursor;
      return cs_;
    }
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
    for (uint64_t id : removed_id) {
      Handle* h = handles_[id];
      Del(id, false);
      delete h;
      handles_.erase(id);
    }
    for (BFile* file : files) 
      if (added_id.find(file->Identifier()) != added_id.end()) {
        Handle* h = new Handle(file);
        handles_.emplace(file->Identifier(), h);
      }

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
  void Redo() {
    std::string key = istat_.key_;
    switch (istat_.type_) {
    case leveldb::IteratorState::kSeekToFirst: {
      SeekToFirst();
    } break;
    case leveldb::IteratorState::kSeekToLast: {
      SeekToLast();
    } break;
    case leveldb::IteratorState::kSeek: {
      Seek(key);
    } break;
    case leveldb::IteratorState::kNext: {
      Seek(key);
      Next();
    } break;
    case leveldb::IteratorState::kPrev: {
      Seek(key);
      Prev();
    } break;
    case leveldb::IteratorState::kNoOperation: {

    } break;
    default:
      break;
    }
  }
  bool FileValid(uint64_t id) {
    Handle* h = handles_[id];
    if (h->isOpened())
      return h->iter_->Valid();
    else {
      Slice ukey(leveldb::ExtractUserKey(
        Valid() ? key() : istat_.key_));
      return h->file_->Max().compare(ukey) >= 0;
    }
  }
  bool VecValid(const std::vector<BFile*>& files) {
    for (BFile* file : files) {
      if (FileValid(file->Identifier()))
        return 1;
    }
    return 0;
  }
 private:
  size_t NextOpenEqual(size_t k, bool echo) {
    if (k >= forward_.size()) return 0;
    std::string key = FMin(k).ToString();
    size_t tail;
    size_t opened = 0;
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
  size_t NextOpenEqual(size_t k, const Slice& upper, bool echo) {
    if (k >= forward_.size()) return 0;
    if (FMin(k).compare(upper) > 0) return 0;
    return NextOpenEqual(k, echo);
  }
  void LocateForward(const Slice& key) {
    for (; forward_curr_ < forward_.size(); ++forward_curr_) {
      if (F(forward_curr_)->UpdateCursor(key) == GreaterThanCursor)
        break;
    }
    for (size_t i = forward_curr_ + 1; i < forward_.size(); ++i)
      F(i)->cs_ = GreaterThanCursor;
  }
 public:  // inherit.
  virtual void SeekToFirst() override { 
    if (N() == 0) return;
    forward_curr_ = 0;
    for (size_t i = 0; i < forward_.size(); ++i)
      F(i)->cs_ = GreaterThanCursor;
    NextOpenEqual(0, false);
    BaseIter::SeekToFirst();
  }
  virtual void SeekToLast() override {
    // unfinished.
    assert(false);
  }
  size_t OpenRange(const Slice& key1, const Slice& key2, bool echo) {
    sagitrs::RealBounded bound(key1, key2);
    size_t opened = 0;
    for (int i = 0; i < forward_.size(); ++i) {
      if (F(i)->file_->Compare(bound) == BOverlap) {
        if (!F(i)->isOpened())
          Open(F(i), echo);
        opened ++;
      }
    }
    return opened;
  }
  virtual void Seek(const Slice& ikey) override {
    Slice ukey(leveldb::ExtractUserKey(ikey));
    size_t opened = 0;
    opened = OpenRange(ukey, ukey, false);
    if (opened > 0)
      BaseIter::Seek(ikey);
    if (opened > 0 && Valid()) {
      Slice key2(leveldb::ExtractUserKey(key()));
      size_t opened2 = OpenRange(ukey, key2, true);
    } else {
      //assert(opened == 0);
      forward_curr_ = 0;
      LocateForward(ukey);
      while (!Valid()) {
        opened = NextOpenEqual(forward_curr_, false);
        if (opened > 0)
          BaseIter::Seek(ikey);
      }
    }
  }
  virtual void NextEnd(uint64_t id) override {
    Handle* h = handles_[id];
    h->cs_ = LessThanCursor;
  } 
  virtual void Next() override {
    // 1. Seek from key1 to key2.
    // 2. Open files between (key1, key2], reopen.
    // 3. if seek failed, open next files.
    assert(Valid());
    BaseIter::Next();
    assert(!istat_.key_.empty());
    Slice key1(leveldb::ExtractUserKey(istat_.key_));
    size_t opened = 0;
    LocateForward(key1);
    if (forward_curr_ >= forward_.size()) {
      // no files to open.
        return;
    }
    if (!Valid()) {
      // current file is finished.
      opened = NextOpenEqual(forward_curr_, true);
      assert(Valid());
    } else {
      Slice ikey2(key());
      assert(!ikey2.empty());
      Slice key2(leveldb::ExtractUserKey(ikey2));
      opened = NextOpenEqual(forward_curr_, key2, true);
    }
  }
  virtual void Prev() override {
    assert(false);
    // unfinished.
  }
  
};

}  // namespace leveldb
