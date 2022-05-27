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
  SBSNode *head_, *prev_;
  size_t height_;

  std::vector<SBSNode*> next_level_;
  int overlap_begin_, overlap_end_;
  std::vector<LevelNode*> dlnodes_;
  std::vector<BFile*> dfiles_;
  // recursive mode : remove node in next_level_[overlap_begin, overlap_end_].
  // normal mode: remove lnode in next_level[...].

  bool recursive_compaction_;
  size_t memory_usage_;

 public:
  SubSBS(SBSNode* head, size_t height, SBSNode* prev)
  : head_(head), prev_(prev), height_(height), 
    next_level_(), overlap_begin_(0), overlap_end_(0),
    recursive_compaction_(height == 1),
    memory_usage_(0)
  {
    auto next = head->Next(height);
    for (SBSNode* node = head_; node != next; node = node->Next(height-1))
      next_level_.push_back(node);
  }
  ~SubSBS() { 
    if (recursive_compaction_) {
      for (int i = overlap_begin_ + 1; i < overlap_end_; ++i) if (i > 0) {
        SBSNode* node = next_level_[i];
        assert(node->Height() == 1);
        delete node;
      }
    }

    for (auto& lnode : dlnodes_)
      delete lnode;
    for (auto& file : dfiles_)
      delete file;
  }

  size_t MemoryUsage() const { return memory_usage_; }
  
  SBSNode* Head() const { return head_; }
  size_t Height() const { return height_; }
  const SBSOptions& Options() const { return head_->options_; }
  
  inline BFile* GetOne(SBSNode* node) {
    const BFileVec& buffer = head_->GetLevel(height_)->buffer_;
    BFile* file = buffer.GetOne();
    return file;
  }
  bool FindOverlap(const std::vector<FileMetaData*>& deleted) {
    std::set<uint64_t> dnums;
    for (auto file : deleted) 
      dnums.insert(file->number);
    
    const BFileVec& buffer = head_->GetLevel(height_)->buffer_;
    RealBounded range(buffer);
    for (BFile* file : buffer) {
      assert(dnums.find(file->Identifier()) != dnums.end());
      dfiles_.push_back(file);
    }

    overlap_begin_ = -1;
    overlap_end_ = next_level_.size();
    for (int i = 0; i < next_level_.size(); ++i) {
      const BFileVec& buffer = next_level_[i]->GetLevel(height_ - 1)->buffer_;
      if (buffer.empty()) continue;
      auto res = buffer.Compare(range);
      if (res == BGreater && overlap_end_ > i) 
        overlap_end_ = i;
      if (res == BLess && overlap_begin_ < i)
        overlap_begin_ = i;
      if (res == BOverlap) {
        assert(overlap_begin_ < i && i < overlap_end_);
        if (recursive_compaction_) {
          auto file = buffer.GetOne();
          dfiles_.push_back(file);  
        }
      }
    }
    assert(dfiles_.size() == dnums.size());
    return 1;
  }

  void Transform(const std::vector<FileMetaData*>& generated_files, std::vector<BFile*>& newchild) {
    sagitrs::BFileVec buffer;
    std::vector<FileGenData> gendata;

    std::vector<FileMetaData*> generated(generated_files);
    std::sort(generated.begin(), generated.end(), [](FileMetaData* a, FileMetaData* b) {
      return a->smallest.user_key().compare(b->smallest.user_key()) < 0;});

    size_t curr = overlap_begin_ + 1;
    for (auto& meta : generated) {
      FileGenData gd; 
      gd.f = meta;
      if (recursive_compaction_) {
        RealBounded meta_range(meta->smallest.user_key(), meta->largest.user_key());
        for (; curr < overlap_end_; ++curr) {
          auto file = next_level_[curr]->GetLevel(height_ - 1)->buffer_.GetOne();
          if (file == nullptr) continue;
          auto res = meta_range.Compare(*file);
          if (res == BGreater) continue;
          if (res == BLess) break;
          assert(res == BOverlap);
          gd.inherit.push_back(file);
        }
      }
      gendata.push_back(gd);
    }
    for (auto& gen : gendata) {
      sagitrs::Statistics s(Options(), Options().NowTimeSlice());
      for (auto& file : gen.inherit) 
        s.MergeStatistics(*file);
      gen.file = new BFile(gen.f, s);
      memory_usage_ += sizeof(gen.file);
    }
    for (auto& gen : gendata)
      newchild.push_back(gen.file);
    // comparison function object (i.e. an object that satisfies the requirements of Compare) 
    // which returns ​true if the first argument is less than (i.e. is ordered before) the second.
    std::sort(newchild.begin(), newchild.end(), [](BFile* a, BFile* b){
      return a->Data()->smallest.user_key().compare(b->Data()->smallest.user_key()) < 0;});
  }
  void CleanUp(SBSNode* node, size_t height) {
    auto old = node->GetLevel(height);
    node->SetLevel(height, new LevelNode(Options(), old->next_));
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
    auto old = node->GetLevel(height);
    dlnodes_.push_back(old);
    node->SetLevel(height, lnode);
    if (lnode) node->Rebound(true);
  }
  void NodePut(BFile* file, SBSNode* node, size_t height) {
    // insert file into node[height].
    auto lnode = BuildLNode(node->GetLevel(height), file, nullptr);
    Replace(node, height, lnode);
  }
  bool TreePut(BFile* file, SBSNode* node, size_t height) {
    SBSNode *tail = head_->Next(height);
    SBSNode* next = nullptr;
    if (height > 1) {
      for (SBSNode* n = node; n != tail; n = next) {
        next = n->Next(height - 1);
        Slice rbound;
        if (next) rbound = next->Guard();
        assert(n->Guard().compare(file->Min()) <= 0);
        int cmp2 = next ? file->Max().compare(rbound) : -1;
        if (cmp2 < 0) { 
          TreePut(file, n, height-1);
          return 1;
        }
        int cmp1 = next ? file->Min().compare(rbound) : -1;
        if (cmp1 < 0)
          break;  // this file covers next node.
        // otherwise, check next.
      }
    }
    // file can not be inserted to child node.
    NodePut(file, node, height);
    return 0;
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

      SBSNode* next = overlap_end_ >= next_level_.size() ? 
                        head_->Next(height_) : 
                        next_level_[overlap_end_];
      for (int i = files.size() - 1; i > 0; --i) {
        SBSNode* node = new SBSNode(Options(), next);
        node->Add(Options(), 0, files[i]);
        //node->SetNext(0, next);
        next = node;
      }
      if (overlap_begin_ == -1) {
        auto lnode = BuildLNode(nullptr, files[0], next);
        Replace(head_, 0, lnode);
      } else {
        SBSNode* node = new SBSNode(Options(), next);
        node->Add(Options(), 0, files[0]);
        next_level_[overlap_begin_]->SetNext(height_ - 1, node);
      }
    } else {
      for (auto file : files) {
        bool dive = TreePut(file, head_, height_);
        if (!dive) {
          bool d = TreePut(file, head_, height_);
          assert(!d);
        }
      }
    }
    SBSNode* next = head_->Next(height_);
    size_t w1 = prev_ ? prev_->GeneralWidth(height_) : 0;
    size_t w2 = head_->GeneralWidth(height_);
    if (prev_ && head_->Height() == height_ + 1 && 
        (w2 < Options().MinWidth() || w1 < Options().MinWidth())) {
      // this lnode is better to be deleted.
      prev_->SetNext(height_, next);
      Replace(head_, height_, nullptr);
      head_->DecHeight();
    } else {
      auto lnode = BuildLNode(nullptr, nullptr, next);
      Replace(head_, height_, lnode);
    }    
    return 1;
  }
  bool Build(const BFileEdit& edit) {
    std::vector<BFile*> newchild;
    bool ok = FindOverlap(edit.deleted_);
    assert(ok);
    Transform(edit.generated_, newchild);
    ok = BuildWith(newchild);
    assert(ok);
    return ok;
  }

  bool CheckExist(SBSNode* list) {
    auto iter = new SBSIterator(list);
    std::set<uint64_t> dnums;
    for (auto file : dfiles_) 
      dnums.insert(file->Identifier());
    
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next()) {
      SBSNode* n = iter->Current().node_;
      for (int h = 0; h < n->Height(); ++h) {
        LevelNode* l = n->GetLevel(h);
        for (auto file : l->buffer_)
          if (dnums.find(file->Identifier()) != dnums.end())
            return 1;
      }
    }
    if (recursive_compaction_) {
      for (iter->SeekToFirst(0); iter->Valid(); iter->Next()) {
        SBSNode* n = iter->Current().node_;
        for (int i = overlap_begin_ + 1; i < overlap_end_; ++i)
          if (i > 0 && n == next_level_[i]) 
            return 1;
      }
    }
    return 0;
  }

  std::string ToString() {
    std::stringstream ss;
    ss << "SubSBS{";
    for (auto file : dfiles_)
      ss << file->Identifier() << ",";
    ss << "}" << std::endl;
    return ss.str();
  }
};

}

/*
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
        NodePut(f, curr, height_-1);
        //curr->Add(Options(), height_ - 1, f);
      }
*/