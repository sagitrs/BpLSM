#pragma once

#include <vector>
#include "sbs_node.h"
#include "bfile.h"
#include "bfile_edit.h"

#include "leveldb/slice.h"

using leveldb::FileMetaData;
using leveldb::Slice;

namespace sagitrs {

struct SubSBS {
  struct FileGenData { 
    FileMetaData* f; 
    std::vector<BFile*> inherit;
    BFile* file; 
    bool operator<(const FileGenData& b) { 
      return f->smallest.user_key().compare(b.f->smallest.user_key()) < 0;
    }
  };
 private:
  SBSNode *head_;
  size_t height_;
  std::vector<SBSNode*> children_;
  std::vector<BFile*> level_[2];

  bool recursive_compaction_;

 public:
  SubSBS(SBSNode* head, size_t height)
  : head_(head), height_(height), 
    children_(), 
    level_(), 
    recursive_compaction_(height == 1) 
  {
    auto next = head->Next(height);
    for (SBSNode* node = head_->Next(height-1); node != next; node = node->Next(height-1))
      children_.push_back(node);
    CollectFiles(level_[0], level_[1]);
  }
  ~SubSBS() { 
    head_->SetManuallyDispose();

    delete head_->level_[height_];
    head_->level_[height_] = nullptr;
    if (recursive_compaction_) {
      delete head_->level_[height_ - 1];
      head_->level_[height_ - 1] = nullptr;
      for (auto node : children_) {
        delete node->level_[height_ - 1];
        node->level_[height_ - 1] = nullptr;
        delete node;
      }
    }
    delete head_;
  }
  
  size_t Height() const { return height_; }
  const SBSOptions& Options() const { return head_->options_; }
  void CollectFiles(std::vector<BFile*>& level0, std::vector<BFile*>& level1) const {
    for (BFile* file : head_->LevelAt(height_)->buffer_)
      level1.push_back(file);
    if (recursive_compaction_) {
      auto file = head_->LevelAt(height_ - 1)->buffer_.GetOne();
      if (file) level0.push_back(file);
      for (SBSNode* node : children_) {
        auto file = node->LevelAt(height_ - 1)->buffer_.GetOne();
        if (file) level0.push_back(file);
      }
    }
  }
  bool Confirm(std::vector<FileMetaData*> files) const {
    //std::vector<BFile*> level[2];
    //CollectFiles(level[0], level[1]);
    if (files.size() != level_[0].size() + level_[1].size())
      return 0;
    std::set<uint64_t> nums;
    for (auto file : files) nums.insert(file->number);
    for (size_t l = 0; l < 2; ++l)
      for (auto file : level_[l]) 
        if (nums.find(file->Data()->number) == nums.end()) 
          return 0;
    return 1;
  }
  void Transform(const std::vector<FileMetaData*>& generated, 
                 std::vector<FileGenData>& gendata) {
    sagitrs::BFileVec buffer;
    
    for (auto& meta : generated) {
      FileGenData gd; gd.f = meta;
      Slice min(meta->smallest.user_key());
      Slice max(meta->largest.user_key());
      for (auto& d : level_[0]) {
        Slice key(d->Data()->smallest.user_key());
        if (min.compare(key) <= 0 && key.compare(max) < 0) {
          gd.inherit.push_back(d);
          //s->MergeStatistics(std::dynamic_pointer_cast<sagitrs::Statistable>(d)); 
        }
      }
      gendata.push_back(gd);
    }
    for (auto& gen : gendata) {
      sagitrs::Statistics s(Options(), Options().NowTimeSlice());
      for (auto& file : gen.inherit) 
        s.MergeStatistics(*file);
      gen.file = new BFile(gen.f, s);
    }
    std::sort(gendata.begin(), gendata.end());
    //for (auto& gen : generate_files) if (gen.inherit.size() > 0)
    //  list_.Put(gen.file);
    //for (auto& gen : generate_files) if (gen.inherit.size() == 0)
    //  list_.Put(gen.file);
    //list_.Reinsert();
  }
  void CleanUp(SBSNode* node, size_t height) {
    auto old = node->level_[height];
    node->level_[height] = new LevelNode(Options(), old->next_);
  }
  SBSNode* BuildWith(const std::vector<FileGenData>& gendata) {
    SBSNode* node = new SBSNode(head_);
    CleanUp(node, height_);
    if (recursive_compaction_)
      CleanUp(node, height_ - 1);
    node->Rebound();
    if (recursive_compaction_) {
      // build nodes from gendata.
      // make sure gendata is sorted.
      assert(!gendata.empty()); 
      assert(height_ == 1);

      node->Add(Options(), 0, gendata[0].file);
      SBSNode* curr = node;
      for (size_t i = 1; i < gendata.size(); ++i) {
        SBSNode* next = new SBSNode(Options(), nullptr);
        next->Add(Options(), 0, gendata[i].file);
        curr->SetNext(0, next);
        curr = next;
      }
      SBSNode* tail = head_->Next(1);
      curr->SetNext(0, tail);
    } else {
      // push {gendata} to {head_, children_}
      SBSNode *curr = head_, *next = head_->Next(height_);
      for (size_t i = 0; i < gendata.size(); ++i) {
        Slice bound;
        if (curr->Next(height_ - 1) != next)
          bound = curr->Next(height_ - 1)->Guard();
        auto& gen = gendata[i];

        while (bound.size() != 0 && gen.f->largest.user_key().compare(bound) >= 0) {
          curr = curr->Next(height_ - 1);
          assert(curr != next);
          if (curr->Next(height_ - 1) != next)
            bound = curr->Next(height_ - 1)->Pacesetter()->Min();
          else 
            bound = Slice();
        }
        assert(gen.f->smallest.user_key().compare(curr->Guard()) >= 0);
        curr->Add(Options(), height_ - 1, gen.file);
      }
    }
    return node;
  }
  SBSNode* Build(const BFileEdit& edit) {
    bool ok = Confirm(edit.deleted_);
    assert(ok);
    std::vector<FileGenData> gendata;
    Transform(edit.generated_, gendata);
    SBSNode* node = BuildWith(gendata);
    return node;
  }

  bool CheckExist(SBSNode* list) {
    auto iter = new SBSIterator(list);
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next()) {
      SBSNode* n = iter->Current().node_;
      if (n == head_) 
        return 1;
      if (recursive_compaction_)
        for (auto& c : children_)
          if (n == c) 
            return 1;
    }
    return 0;
  }
};

}