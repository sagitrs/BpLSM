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
  SBSNode* head_;
  size_t height_;
  std::vector<SBSNode*> children_;

  std::vector<BFile*> level_[2];
  
  std::vector<LevelNode*> lnodes_;
  std::vector<SBSNode*> nodes_;
  bool recursive_compaction_;

 public:
  SubSBS(SBSNode* head, size_t height)
  : head_(head), height_(height), children_(), 
    lnodes_(), nodes_(), 
    level_(), recursive_compaction_(height == 1) 
  {
    auto next = head->Next(height);
    for (SBSNode* node = head_->Next(height-1); node != next; node = node->Next(height-1))
      children_.push_back(node);
    CollectFiles(level_[0], level_[1]);
  }
  ~SubSBS() { 
    // files in lnode shall not be deleted, since handle is inherited by new node.
    for (auto& lnode : lnodes_) {
      std::vector<BFile*> handler;
      for (auto& file : lnode->buffer_)
        handler.push_back(file);
      for (auto& file : handler) {
        auto popped = lnode->Del(*file);
        assert(popped == file);
      }
      delete lnode;
    }
    // files in node shall be deleted.
    for (auto& node : nodes_)
      delete node;
  }
  
  SBSNode* Head() const { return head_; }
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
  bool PickOldFiles(const std::vector<FileMetaData*>& files, std::vector<BFile*>& oldchild) const {
    if (files.size() != level_[0].size() + level_[1].size())
      assert(level_[0].size() > 0);
    
    std::set<uint64_t> nums;
    for (auto file : files) nums.insert(file->number);
    for (size_t l = 0; l < 2; ++l)
      for (auto file : level_[l]) 
        if (nums.find(file->Data()->number) == nums.end()) {
          assert(l == 0);
          if (l == 1) return 0;
          // this file locates in child level, but not chosen for compaction.
          for (auto f : files) {
            RealBounded fbound(f->smallest.user_key(), f->largest.user_key());
            if (file->Compare(fbound) == BOverlap)
              return 0;
          }
          // so it shall has no overlap with any other files.
          oldchild.push_back(file);
        }
    return 1;
  }
  void Transform(const std::vector<FileMetaData*>& generated, 
                 const std::vector<BFile*>& oldchild,
                 std::vector<BFile*>& newchild) {
    sagitrs::BFileVec buffer;
    std::vector<FileGenData> gendata;

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
    for (auto& gen : gendata)
      newchild.push_back(gen.file);
    for (auto& old : oldchild)
      newchild.push_back(old);
    // comparison function object (i.e. an object that satisfies the requirements of Compare) 
    // which returns ​true if the first argument is less than (i.e. is ordered before) the second.
    std::sort(newchild.begin(), newchild.end(), [](BFile* a, BFile* b){
      return a->Data()->smallest.user_key().compare(b->Data()->smallest.user_key()) < 0;});
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
  LevelNode* BuildLNode(LevelNode* base, BFile* file, SBSNode* next) {
    auto node = (base != nullptr ? 
      new LevelNode(*base) :
      new LevelNode(Options(), next));
    if (base && next)
      node->next_ = next;
    if (file)
      node->Add(file);
    return node;
  }
  void Replace(SBSNode* node, size_t height, LevelNode* lnode) {
    lnodes_.push_back(node->level_[height]);
    node->level_[height] = lnode;
    node->Rebound();
  }
  bool BuildWith(const std::vector<BFile*>& files) {
    if (recursive_compaction_) {
      // build nodes from gendata.
      // make sure gendata is sorted.
      if (files.empty()) {
        assert(false && "No file generated.");
        return false;
      } 
      assert(height_ == 1);

      // will be deleted when destructed.
      for (auto& child : children_)
        nodes_.push_back(child);

      SBSNode* next = head_->Next(height_);
      for (int i = files.size() - 1; i > 0; --i) {
        SBSNode* node = new SBSNode(Options(), next);
        node->Add(Options(), 0, files[i]);
        //node->SetNext(0, next);
        next = node;
      }
      auto lnode = BuildLNode(nullptr, files[0], next);
      Replace(head_, 0, lnode);
    } else {
      // push {gendata} to {head_, children_}
      SBSNode *curr = head_, *next = head_->Next(height_);
      for (size_t i = 0; i < files.size(); ++i) {
        Slice bound;
        if (curr->Next(height_ - 1) != next)
          bound = curr->Next(height_ - 1)->Guard();
        auto& f = files[i];

        while (bound.size() != 0 && f->Data()->largest.user_key().compare(bound) >= 0) {
          curr = curr->Next(height_ - 1);
          assert(curr != next);
          if (curr->Next(height_ - 1) != next)
            bound = curr->Next(height_ - 1)->Pacesetter()->Min();
          else 
            bound = Slice();
        }
        assert(f->Data()->smallest.user_key().compare(curr->Guard()) >= 0);
        
        auto lnode = BuildLNode(curr->level_[height_-1], f, nullptr);
        Replace(curr, height_ - 1, lnode);
        //curr->Add(Options(), height_ - 1, f);
      }
    }
    SBSNode* next = head_->level_[height_]->next_;
    auto lnode = BuildLNode(nullptr, nullptr, next);
    Replace(head_, height_, lnode);
    return 1;
  }
  bool Build(const BFileEdit& edit) {
    std::vector<BFile*> oldchild, newchild;
    bool ok = PickOldFiles(edit.deleted_, oldchild);
    if (!ok) {
      // Error: data not fit.
      assert(ok);
      std::cout << "Build Error: confirm failed." << std::endl;
      std::cout << "Level0={"; for (auto& f : level_[0]) std::cout << f->ToString() << ","; std::cout << "}" << std::endl;
      std::cout << "Level1={"; for (auto& f : level_[1]) std::cout << f->ToString() << ","; std::cout << "}" << std::endl;
      std::cout << edit.ToString() << std::endl;
      return 0;
    }
    Transform(edit.generated_, oldchild, newchild);
    ok = BuildWith(newchild);
    return ok;
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