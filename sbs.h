#pragma once

#include <vector>
#include <stack>
#include <algorithm>
#include <math.h>
#include "sbs_node.h"
#include "sbs_iterator.h"
#include "delineator.h"
#include "sublist.h"

#include "scorer_impl.h"
#include "sampler.h"
namespace sagitrs {
struct Scorer;
struct SBSkiplist {
  friend struct Scorer;
  typedef BFile* TypeValuePtr;
  typedef SBSNode TypeNode;
  SBSOptions options_;
 private:
  SBSNode* head_;
 public:
  SBSkiplist(const SBSOptions& options) 
  : options_(options),
    head_(new SBSNode(options_, options_.kMaxHeight())) {}
  inline SBSIterator* NewIterator() const { return new SBSIterator(head_); }
  
  ~SBSkiplist() {
    std::vector<SBSNode*> list;
    for (SBSNode* node = head_; node != nullptr; node = node->Next(0))
      list.push_back(node);
    for (auto& node : list) {
      node->ReleaseAll();
      delete node;
    }
  }
  void ReplaceHead(SBSNode* new_head) { head_ = new_head; }
  void Put(BFile* value) {
    auto iter = NewIterator();
    bool state = PutBlocked(value, iter);
    if (!state) {
      BFileVec container;
      assert(iter->Current().TestState(options_) > 0);
      iter->Current().SplitNext(options_, &container);
      for (auto &v : container) {
        PutBlocked(v, iter);
      }
    }
    delete iter;
  }
  bool PutBlocked(BFile* value, SBSIterator* iter) {
    iter->SeekToRoot();
    bool state = iter->Add(options_, value);
    return state;
    //iter.TargetIncStatistics(value->Min(), DefaultCounterType::PutCount, 1);                          // Put Statistics.
  }
  
  int SeekHeight(const Bounded& range) {
    auto iter = NewIterator();
    iter->SeekToRoot();
    iter->SeekRange(range, true);
    int height = iter->Current().height_;
    delete iter;
    return height;
  }
  inline void LookupKey(const Slice& key, BFileVec& container) const {
    auto iter = NewIterator();
    iter->SeekToRoot();
    RealBounded bound(key, key);
    iter->SeekRange(bound);
    //std::cout << iter.ToString() << std::endl;
    iter->GetBufferOnRoute(container, key);
    delete iter;
  }
  SubSBS* LookupTree(const BFileEdit& edit) {//, std::vector<SBSNode*>& prev
    auto iter = NewIterator();
    bool found = false;
    Coordinates suspect(nullptr, 0);
    for (auto file : edit.deleted_) {
      RealBounded bound(file->smallest.user_key(),file->smallest.user_key());
      iter->SeekToRoot();
      iter->SeekRange(bound);
      BFile* target = iter->SeekValueInRoute(file->number);
      size_t height = iter->Current().height_;
      if (height == 0) { 
        if (!found) {
          iter->Float(); 
          suspect = iter->Current();
          found = 1;
        }
        assert(suspect.height_ == 1);
        break;
      }
      if (!found) {
        suspect = iter->Current();
        found = 1;
        continue;
      }
      Coordinates current = iter->Current();
      if (suspect.height_ == current.height_) {
        if (suspect.node_ == current.node_)
          continue; // search for another node.
        // they have common parent.
        iter->Float();
        auto iter2 = NewIterator();
        iter2->SeekNode(suspect);
        iter2->Float();
        assert(iter->Current() == iter2->Current());
        suspect = iter->Current();
        delete iter2;
        break;
      } else {
        if (suspect.height_ < current.height_) {
          Coordinates mid = current; 
          current = suspect;
          suspect = mid;
          iter->SeekNode(current);
        }
        //suspect shall be current's parent.
        iter->Float();
        assert(iter->Current() == suspect);
        break;
      }
    }
    iter->SeekNode(suspect);
    iter->Prev();
    SBSNode* prev = iter->Current().node_;
    delete iter;
    return new SubSBS(suspect.node_, suspect.height_, prev);
  }
  void UpdateStatistics(const BFile& file, uint32_t label, int64_t diff, int64_t time) {
    auto iter = NewIterator();
    iter->SeekToRoot();
    iter->SeekRange(file);
    auto target = iter->SeekValueInRoute(file.Identifier());
    if (target == nullptr) {
      // file is deleted when bversion is unlocked.
      return;
    }
    //Statistics::TypeTime now = options_->NowTimeSlice();
    target->UpdateStatistics(label, diff, time);
    iter->SetRouteStatisticsDirty();
    //iter->UpdateRouteHottest(target);
    delete iter;
  }
  BFile* Pop(const BFile& file, bool auto_reinsert = true) {
    auto iter = NewIterator();
    iter->SeekToRoot();
    //auto target = std::dynamic_pointer_cast<BoundedValue>(value);
    auto res = iter->Del(options_, file, auto_reinsert);
    delete iter;
    return res;
  }
  void PickCompactionFilesByIterator(const sagitrs::SBSOptions& options,
                                     SBSIterator* iter, BFileVec* containers) {
    if (containers == nullptr) return;
    
    BFileVec& base_buffer = containers[0];
    BFileVec& child_buffer = containers[1];
    BFileVec& guards = containers[2];
    BFileVec& l0guards = containers[3];
    // get file in current.
    iter->GetBufferInCurrent(base_buffer);
    // if last level, pick files into this compaction, otherwise push to guards.
    int height = iter->Current().height_;
    
    bool global_compaction = 
      iter->Current().node_ == head_ && iter->Current().Next() == nullptr;

    auto st = iter->Current(); st.JumpDown();
    auto ed = iter->Current(); ed.JumpNext(); ed.JumpDown();
    
    if (height == 1) {
      // pick last level file into compactor.
      for (Coordinates c = st; c.Valid() && !(c == ed); c.JumpNext()) {
        auto& l0buffer = c.node_->GetLevel(0)->buffer_;
        if (l0buffer.size() == 0) continue;
        auto l0file = l0buffer.at(0);
        //auto pacesster
        if (base_buffer.Compare(*l0file) != BOverlap) continue;
        child_buffer.push_back(l0file);
        l0guards.push_back(l0file);
      }
    } else {/*
      for (Coordinates c = st; c.Valid() && !(c == ed); c.JumpNext()) {
        auto& buffer = c.node_->GetLevel(height - 1)->buffer_;
        if (buffer.size() == 0) continue;
        for (auto file : buffer) {
          if (file->Data()->file_size >= options_.MaxFileSize() / 5)
            continue;
          if (base_buffer.Compare(*file) != BOverlap) 
            continue;
          child_buffer.push_back(file);
        }
      }*/
      PickGuard(options, guards, iter->Current(), options.force_pick_);
      Filter(guards, base_buffer);
    }
  }
  void Filter(BFileVec& vec, const Bounded& range) {
    BFileVec result;
    for (BFile* file : vec) 
      if (range.Compare(*file) == BOverlap)
        result.push_back(file);
    //std::sort()
    if (result.size() != vec.size()) {
      vec.clear();
      vec.AddAll(result);
    }
  }
  struct Shard {
    Coordinates coord_;
    BFile* guard_file_;
    size_t sample_covers_;
    bool picked_;
    Shard(const Coordinates& coord, BFile* file, size_t size)
      : coord_(coord), guard_file_(file), sample_covers_(size),
        picked_(false) {}
  };
  void PickShard(std::vector<Shard>& shards, sagitrs::Coordinates parent, 
                 SamplerTable* table) {
    auto st = parent; st.JumpDown();
    auto ed = parent; ed.JumpNext(); ed.JumpDown();
    size_t prev = 0;
    Coordinates prev_coord(st);
    BFile* prev_guard = nullptr;
    for (Coordinates c = st; c.Valid() && !(c == ed); c.JumpNext()) {
      auto guard = c.node_->Pacesetter();
      //if (l0buffer.size() == 0) continue;
      //auto l0file = l0buffer.at(0);
      //auto pacesster
      Slice min_key(guard ? guard->Min() : Slice(""));
      int curr = table ? table->GetCountSmallerOrEqualThan(min_key) : 0;
      if (!(c == st)) { 
        int size = curr - prev;
        shards.emplace_back(prev_coord, prev_guard, size);
      }
      prev = curr;
      prev_coord = c;  
      prev_guard = guard;
    }
    BFile* last_file = nullptr;
    if (ed.Valid()) {
      last_file = ed.node_->Pacesetter();
      //auto& l0buffer = ed.node_->GetLevel(0)->buffer_;
      //last_file = l0buffer.at(0);
    } else {
      auto iter = NewIterator();
      iter->SeekToLast(0);
      last_file = iter->Current().Buffer().GetOne();
    }
    {
      Slice min_key(last_file->Min());
      int curr = table ? table->GetCountSmallerOrEqualThan(min_key) : 0;
      if (!(ed == st)) { 
        int size = curr - prev;
        shards.emplace_back(prev_coord, prev_guard, size);
      }
    }
  }

  bool PickGuard(const sagitrs::SBSOptions& options, BFileVec& guards, 
                 sagitrs::Coordinates parent, bool force_pick) {
    std::vector<Shard> shards;
    bool guard_picked = false;
    PickShard(shards, parent, options.table_);
    for (size_t i = 0; i < shards.size(); ++i) {
      Shard& tree = shards[i];
      size_t insize = options.MaxFileSize();
      size_t outsize = parent.Table()[MinHoleFileSize];
      size_t limit = options.SamplePerInputFile() * insize / outsize;
      bool out_of_size = tree.sample_covers_ >= limit;
      bool divable = parent.height_ >= 3;
      if (force_pick || out_of_size) {
        if (i > 0)
          tree.picked_ = 1;
        if (i + 1 < shards.size())
          shards[i+1].picked_ = 1;
      }
      if (out_of_size && divable)
        PickGuard(options, guards, tree.coord_, false);
      if (tree.picked_ && tree.guard_file_) {
        guards.push_back(tree.guard_file_);
        guard_picked = 1;
      }
    }
    return guard_picked;
  }
  
  SBSIterator* NewScoreIterator(Scorer& scorer, double baseline, double& score) {
    SBSIterator* iter = NewIterator();
    iter->SeekToRoot();
    iter->UpdateAllTable();
    score = iter->SeekScore(scorer, baseline, baseline != 0);
    if (scorer.isUpdated()) {
      return iter;
    } else {
      delete iter;
      return nullptr;
    }
  }
 private:
  void PrintDetailed(std::ostream& os) const {
    os << "----------Print Detailed Begin----------" << std::endl;
    head_->ForceUpdateStatistics();
    for (auto i = head_; i != nullptr; i = i->Next(0))
      os << i->ToString();
    os << "----------Print Detailed End----------" << std::endl;  
  }
  void PrintList(std::ostream& os) const {
    struct NodeStatus : public Printable {
      struct ValueStatus {
        size_t width_;
        uint64_t id_;
        uint64_t size_;
      };
      std::vector<sagitrs::Printable::KVPair> ns_;
      std::vector<ValueStatus> vs_;
      size_t width_;
      virtual void GetStringSnapshot(std::vector<KVPair>& snapshot) const override {
        for (auto& kv : ns_) snapshot.push_back(kv);
        for (auto& vs : vs_) {
          snapshot.emplace_back(std::to_string(vs.id_), std::to_string(vs.size_ / 1024) + "K" 
            + "|" + std::to_string(vs.width_) + "/" + std::to_string(width_));
        }
      }
    };
    auto iter = NewIterator();
    std::vector<std::vector<NodeStatus>> map;
    size_t maxh = 0;
    os << "----------Print List Begin----------" << std::endl;
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next()) {
      auto node = iter->Current().node_;
      auto height = node->Height();
      map.emplace_back();
      std::vector<NodeStatus>& lns = *map.rbegin();
      for (int h = 0; h < height; h ++) {
        lns.emplace_back(); NodeStatus& status = *lns.rbegin();//NodeStatus status;
        //iter->SeekNode(c);
        BFileVec children;
        node->GetChildGuard(h, &children);
        //iter->GetChildGuardInCurrent(children);
        auto& buffer = node->GetLevel(h)->buffer_;
        for (auto value : buffer) {
          NodeStatus::ValueStatus vs;
          vs.width_ = children.GetValueWidth(*value);
          vs.size_ = value->Size();
          vs.id_ = value->Identifier();
          status.vs_.push_back(vs);
        }
        buffer.GetStringSnapshot(status.ns_);
        status.width_ = children.size();
        //lns.push_back(status);
        //iter->SeekNode(Coordinates(node, 0));
      }
      if (height > maxh) maxh = height;
    }
    for (int h = maxh - 1; h >= 0; --h) {
      std::vector<std::vector<Printable::KVPair>> print_list;
      int max = 0;
      for (size_t i = 0; i < map.size(); ++i) {
        print_list.emplace_back();
        std::vector<Printable::KVPair>& node_list = print_list[i];
        if (map[i].size() > h) {
          map[i][h].GetStringSnapshot(node_list);
          if (node_list.size() > max) max = node_list.size();
        }
      }
      static const size_t PrintWidth = 20;
      for (int l = -1; l <= max; ++l) {
        if (l == -1 || l == max) {
          bool prev = 0;
          bool curr = 0;
          for (size_t i = 0; i < print_list.size(); ++i) {
            curr = print_list[i].size() > 0;
            os << ((prev || curr) ?  "+" : " ");
            os << std::string(PrintWidth, (curr ? '-' : ' '));
            prev = curr;
          }
          os << (curr ? '+' : ' ');
        } else {
          bool prev = 0;
          bool curr = 0;
          bool line = 0;
          for (size_t i = 0; i < print_list.size(); ++i) {
            curr = print_list[i].size() > 0;
            line = print_list[i].size() > l;
            os << ((prev || curr) ?  "|" : " ");
            os << (line ? Printable::KVPairToString(print_list[i][l], PrintWidth) : std::string(PrintWidth,' '));
            prev = curr;
          }
          os << (curr ? '|' : ' ');
        }
        os << std::endl;
      }
    }
    os << "----------Print List End----------" << std::endl;
    delete iter;
  }
 
  void OldPrintSimple(std::ostream& os) const {
    auto iter = NewIterator();
    std::vector<size_t> hs;
    size_t maxh = 0;
    os << "----------Print Simple Begin----------" << std::endl;
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next()) {
      auto height = iter->Current().node_->Height();
      hs.push_back(height);
      if (height > maxh) maxh = height;
    }
    for (int h = maxh; h > 0; --h) {
      for (size_t i = 0; i < hs.size(); ++i)
        os << (hs[i] >= h ? '|' : ' ');
      os << std::endl;
    }
    os << "----------Print Simple End----------" << std::endl;
    delete iter;
  }
  void PrintSimple(std::ostream& os) const {
    auto iter = NewIterator();
    std::vector<std::vector<size_t>> map;
    size_t maxh = 0;
    os << "----------Print Hole Begin----------" << std::endl;
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next()) {
      auto height = iter->Current().node_->Height();
      std::vector<size_t> height_state;
      for (size_t i = 0; i < height; ++i)
        height_state.push_back(iter->Current().node_->GetLevel(i)->buffer_.HoleSize());
      map.push_back(height_state);
      if (height > maxh) maxh = height;
    }
    for (int h = maxh - 1; h >= 0; --h) {
      for (size_t i = 0; i < map.size(); ++i) {
        if (map[i].size() > h) {
          size_t x = map[i][h];
          char ch = (x <= 9 ? ('0' + x) : (x < 36 ? ('A' + x - 10) : '@'));
          os << ch;
        } else 
          os << ' ';
      }
      os << std::endl;
    }
    os << "----------Print Hole End----------" << std::endl;
    delete iter;
  }
  void PrintCapacitySimple(std::ostream& os) const {
    auto iter = NewIterator();
    std::vector<std::vector<size_t>> map;
    size_t maxh = 0;
    os << "---------- Print Max Run ----------" << std::endl;
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next()) {
      auto height = iter->Current().node_->Height();
      std::vector<size_t> height_state;
      for (size_t i = 0; i < height; ++i) {
        size_t max_runs = iter->Current().node_->GetLevel(i)->table_[HoleFileCapacity];
        //assert(max_runs >= 0);
        if (max_runs > options_.MaxWidth() * options_.DefaultWidth()) {
          std::cout << "Error : Invalid Max Runs." << std::endl;
          assert(false);
        }
        height_state.push_back(max_runs);
      }
      map.push_back(height_state);
      if (height > maxh) maxh = height;
    }
    for (int h = maxh - 1; h >= 0; --h) {
      for (size_t i = 0; i < map.size(); ++i) {
        if (map[i].size() > h) {
          size_t x = map[i][h];
          char ch = (x <= 9 ? ('0' + x) : (x < 36 ? ('A' + x - 10) : '@'));
          //if (ch == '@') assert(false);
          os << ch;
        } else 
          os << ' ';
      }
      os << std::endl;
    }
    os << "----------Print Tape File End----------" << std::endl;
    delete iter;
  }
  void PrintWriteReadSimple(std::ostream& os) const {
    auto iter = NewIterator();
    std::vector<std::vector<size_t>> map;
    size_t maxh = 0;
    os << "-------- Print Write-Read Rate --------" << std::endl;
    double time = options_.TimeSliceMicroSecond() / 1000 / 1000;
    int64_t now = options_.NowTimeSlice();
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next()) {
      auto height = iter->Current().node_->Height();
      std::vector<size_t> height_state;
      for (size_t i = 0; i < height; ++i) {
        uint64_t read = 0, write = 0;
        if (i > 0) {
          read = iter->Current().node_->GetLevel(i)->table_[LocalGet];
          write = iter->Current().node_->GetLevel(i)->table_[LocalWrite];
        }
        else {
          SBSNode* node = iter->Current().node_;
          const Statistics* stats = node->GetTreeStatistics(i);
          if (stats) { 
            read = stats->GetStatistics(KSGetCount, now - 1) / time;
            write = stats->GetStatistics(KSPutCount, now - 1) / time;
          }
        }
        size_t r = (read+write) == 0 ? 0 : 10 * write / (read + write);
        height_state.push_back(r);
      }
      map.push_back(height_state);
      if (height > maxh) maxh = height;
    }
    for (int h = maxh - 1; h >= 0; --h) {
      for (size_t i = 0; i < map.size(); ++i) {
        if (map[i].size() > h) {
          size_t x = map[i][h];
          char ch = (x <= 9 ? ('0' + x) : (x < 36 ? ('A' + x - 10) : '@'));
          os << ch;
        } else 
          os << ' ';
      }
      os << std::endl;
    }
    os << "----------Print Tape File End----------" << std::endl;
    delete iter;
  }
  void PrintHotSimple(std::ostream& os) const {
    auto iter = NewIterator();
    std::vector<std::vector<size_t>> map;
    size_t maxh = 0;
    os << "-------- Print Hot Rate --------" << std::endl;
    double time = options_.TimeSliceMicroSecond() / 1000 / 1000;
    int64_t now = options_.NowTimeSlice();
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next()) {
      auto height = iter->Current().node_->Height();
      std::vector<size_t> height_state;
      for (size_t i = 0; i < height; ++i) {
        uint64_t read = 0, write = 0;
        if (i > 0) {
          read = iter->Current().node_->GetLevel(i)->table_[LocalGet];
          write = iter->Current().node_->GetLevel(i)->table_[LocalWrite];
        }
        else {
          SBSNode* node = iter->Current().node_;
          const Statistics* stats = node->GetTreeStatistics(i);
          if (stats) { 
            read = stats->GetStatistics(KSGetCount, now - 1) / time;
            write = stats->GetStatistics(KSPutCount, now - 1) / time;
          }
        }
        uint64_t total = read+write;
        size_t r = total > 1 ? std::log10(total) : 0;
        height_state.push_back(r);
      }
      map.push_back(height_state);
      if (height > maxh) maxh = height;
    }
    for (int h = maxh - 1; h >= 0; --h) {
      for (size_t i = 0; i < map.size(); ++i) {
        if (map[i].size() > h) {
          size_t x = map[i][h];
          char ch = (x <= 9 ? ('0' + x) : (x < 36 ? ('A' + x - 10) : '@'));
          os << ch;
        } else 
          os << ' ';
      }
      os << std::endl;
    }
    os << "----------Print Tape File End----------" << std::endl;
    delete iter;
  }
  void PrintSmallFileSimple(std::ostream& os) const {
    auto iter = NewIterator();
    std::vector<std::vector<size_t>> map;
    size_t maxh = 0;
    os << "----------Print Tape File----------" << std::endl;
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next()) {
      auto height = iter->Current().node_->Height();
      std::vector<size_t> height_state;
      for (size_t i = 0; i < height; ++i)
        height_state.push_back(iter->Current().node_->GetLevel(i)->buffer_.TapeSize());
      map.push_back(height_state);
      if (height > maxh) maxh = height;
    }
    for (int h = maxh - 1; h >= 0; --h) {
      for (size_t i = 0; i < map.size(); ++i) {
        if (map[i].size() > h) {
          size_t x = map[i][h];
          char ch = (x <= 9 ? ('0' + x) : (x < 36 ? ('A' + x - 10) : '@'));
          os << ch;
        } else 
          os << ' ';
      }
      os << std::endl;
    }
    os << "----------Print Tape File End----------" << std::endl;
    delete iter;
  }
  void PrintStatistics(std::ostream& os) const {
    os << "----------Print Statistics Begin----------" << std::endl;
    Delineator d;
    auto iter = NewIterator();
    // return merged statistics.
    //for (iter->SeekToFirst(0); iter->Valid(); iter->Next())
    //  d.AddStatistics(iter->Current().node_->Guard(), iter->GetRouteMergedStatistics());
    // return only last level statistics.
    for (iter->SeekToFirst(0); iter->Valid(); iter->Next())
      if (iter->Current().Buffer().size() == 1)
        d.AddStatistics(iter->Current().node_->Guard(), *iter->Current().Buffer().GetStatistics());
    auto now = options_.NowTimeSlice();
    os << "----------Print KSGet----------" << std::endl;
    d.PrintTo(os, now, KSGetCount);
    os << "----------Print KSPut----------" << std::endl;
    d.PrintTo(os, now, KSPutCount);
    os << "----------Print KSIterate----------" << std::endl;
    d.PrintTo(os, now, KSIterateCount);
    os << "----------Print Statistics End----------" << std::endl;
    delete iter;
  }
 public:
  std::string ToString() const {
    std::stringstream ss;
  #if defined(MINIMUM_BVERSION_PRINT)
    PrintSimple(ss);
    //PrintSmallFileSimple(ss);
    PrintCapacitySimple(ss);
    //PrintWriteReadSimple(ss);
    //PrintHotSimple(ss);
  #else
    PrintList(ss);
    //PrintStatistics(ss);
  #endif
    //PrintStatistics(ss);
    return ss.str();
  }
  size_t size() const {
    size_t total = 0;
    SBSIterator iter(head_);
    iter.SeekToRoot();
    size_t H = iter.Current().height_;

    for (int h = H; h >= 0; --h) {
      iter.SeekToRoot();
      for (iter.Dive(H - h); iter.Valid(); iter.Next()) {
        total += iter.Current().Buffer().size();
      }
    }
    return total;

  }
  SBSNode* GetHead() const { return head_; }
  bool isDirty() const {
    auto iter = NewIterator();
    iter->SeekDirty();
    bool dirty = (iter->Current().height_ > 0);
    delete iter;
    return dirty;
  }

  void ClearHottest() {
    for (auto node = head_; node != nullptr; node = node->Next(0)) {
      size_t height = node->Height();
      for (size_t h = 1; h < height; ++h)
        node->GetLevel(h)->table_.hottest_ = nullptr;
    }
  }

  bool CheckSplit(Coordinates coord) {
    SBSIterator iter(head_);
    iter.SeekNode(coord);
    return iter.CheckSplit(options_);
  }
  void CheckAbsorb(Coordinates coord) {
    if (coord.height_ + 1 != coord.node_->Height()) 
      return; 
    {
      SBSIterator iter(head_);
      iter.SeekNode(coord);
      iter.Prev();
      iter.CheckAbsorbOnlyNext(options_);
      //if (iter.Valid() && iter.Current().Next() == coord.node_)
      //  iter.CheckAbsorbEmptyNext(options_);
    }
  }
  size_t Level0Size(size_t* cap = nullptr) {
    for (size_t i = 0; i < head_->Height(); ++i) {
      LevelNode* lnode = head_->GetLevel(i);
      if (head_->Next(i) == nullptr) {
        size_t size = lnode->buffer_.HoleSize();
        if (cap)
          *cap = lnode->table_[HoleFileCapacity];
        return size;
      }
    }
    return 0;
  }
};

}

/*

  //std::vector<std::pair<size_t, SBSNode::ValuePtr>>& Recycler() { return iter_.Recycler(); }

  bool HasScore(Scorer& scorer, double baseline) {
    assert(false); // dont use, too slow.
    iter_.SeekToRoot();
    iter_.SeekScore(scorer, baseline, false);
    return scorer.isUpdated();
  }
  void AddAll(const BFileVec& container) {
    for (auto range : container)
      iter_.Add(options_, range);
  }
    // in a special case, too few children in this level.
    if (height >= 2 && iter_.Current().Width() < options_->MinWidth()) {
      for (iter_.Dive(); iter_.Valid() && !(iter_.Current() == ed); iter_.Next()) {
        iter_.GetBufferInCurrent(base_buffer);
      }
      st.JumpDown(); ed.JumpDown();
    }
*/