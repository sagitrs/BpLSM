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
  const Handle* F(size_t k) const { 
    assert(k < forward_.size());
    auto p = handles_.find(forward_[k]);
    assert(p != handles_.end());
    return p->second; 
  }
  Slice FMin(size_t k) const { return F(k)->file_->Min(); }
  Slice FMax(size_t k) const { return F(k)->file_->Max(); }
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
  virtual void Seek(const Slice& ikey) override {
    //std::vector<Handle*> 
    Slice key(leveldb::ExtractUserKey(ikey));
    for (forward_curr_ = 0; forward_curr_ < forward_.size(); ++forward_curr_)
      if (FMin(forward_curr_).compare(key) >= 0)
        break;
    size_t opened = 0;
    for (size_t i = 0; i < forward_curr_; ++i) {
      if (FMin(i).compare(key) <= 0 && FMax(i).compare(key) >= 0) {
        if (!F(i)->isOpened())
          Open(F(i), false);
        opened ++;
      }
    }
    if (opened > 0) {
      BaseIter::Seek(key);
      assert(Valid());
      return;
    }
    // no file covers this key. Any file larger than this key?
    if (forward_curr_ == forward_.size()) {
      BaseIter::Seek(key);
      assert(!Valid());
      return;
    }
    // no file covers this key, but a result may be needed.
    std::string minkey = FMin(forward_curr_).ToString();
    size_t tail;
    for (tail = forward_curr_ + 1; tail < forward_.size(); ++tail) {
      int cmp = FMin(tail).compare(minkey);
      if (cmp == 0) continue;
      if (cmp < 0) assert(false);
      break;
    }
    for (size_t i = forward_curr_; i < tail; ++i) {
      if (!F(i)->isOpened())
        Open(F(i), false);
      opened ++;
    }
    BaseIter::Seek(key);
    assert(Valid());  
  }
  virtual void Next() override {
    BaseIter::Next();
    Slice key1(leveldb::ExtractUserKey(istat_.key_));
    Slice key2(leveldb::ExtractUserKey(key()));
    size_t opened = 0;
    for (; forward_curr_ < N() && FMin(forward_curr_).compare(key2) <= 0; ++forward_curr_) {
      Handle* h = F(forward_curr_);
      if (!h->isOpened())
        Open(h, true);
      opened ++; 
    }
  }
  virtual void Prev() override {
    assert(false);
    // unfinished.
  }
  
};

}  // namespace leveldb
