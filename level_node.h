#pragma once

#include <vector>
#include <stack>
#include <memory>
#include "db/dbformat.h"
#include "bounded.h"
#include "bounded_value_container.h"
#include "options.h"
#include "statistics.h"
namespace sagitrs {
struct SBSNode;

typedef BoundedValueContainer TypeBuffer;
//TODO: Buffer shall be sorted!!!!!
struct ParaTable {
 protected:
  Statistics stats_;
 public:
  ParaTable(const ParaTable& table) = default;
  // Initialize the head node from the option.
  virtual std::shared_ptr<ParaTable> BuildBlank() const = 0;
  virtual std::shared_ptr<ParaTable> BuildCopy() const = 0;
  // When a node inherits the contents of the original node in proportion k, 
  // ParaTable must give an initialization scheme.
  virtual void Shrink(double k) = 0;
  // When a node merges another node, ParaTable must give a merging scheme.
  virtual void Absorb(const ParaTable& bro) = 0;
  void IncStatistics(Counter::TypeLabel label, size_t size) { stats_.Inc(label, size); }
  const Statistics& GetStatistics() const { return stats_; }
};

struct MyParaTable : public ParaTable {
  MyParaTable(const MyParaTable& table) = default;
  virtual void ConstructByOptions(std::shared_ptr<SBSOptions> options) override {
    stats_.ConstructByOptions(std::dynamic_pointer_cast<StatisticsOptions>(options));
  }
  virtual std::shared_ptr<ParaTable> BuildBlank() const {
    auto tmp = std::make_shared<MyParaTable>();
    tmp->stats_.ConstructByOptions(stats_.options_);
    return tmp;
  }
  virtual std::shared_ptr<ParaTable> BuildCopy() const override {
    auto tmp = std::make_shared<MyParaTable>(*this);
    return tmp;
  }
  virtual void Shrink(double k) override { stats_.Shrink(k); }
  virtual void Absorb(const ParaTable& bro) override { stats_.Absorb(bro.GetStatistics()); }
};

struct LevelNode {
  std::shared_ptr<SBSNode> next_;
  TypeBuffer buffer_;
  std::shared_ptr<ParaTable> paras_;
  
  LevelNode() : next_(nullptr), buffer_() {}
  LevelNode(std::shared_ptr<SBSNode> next) 
  : next_(next), buffer_() {}
  void Add(std::shared_ptr<BoundedValue> value) { buffer_.Add(value); }
  void Del(std::shared_ptr<BoundedValue> value) { buffer_.Del(*value); }
  bool Contains(std::shared_ptr<BoundedValue> value) const { return buffer_.Contains(*value); }
  bool Overlap() const { return buffer_.Overlap(); }
  void Absorb(const LevelNode& node) {
    next_ = node.next_;
    buffer_.AddAll(node.buffer_);
    paras_->Absorb(*node.paras_);
  }
  void Shrink(double k) { paras_->Shrink(k); }
  bool isDirty() const { return !buffer_.empty(); }
  void BuildBlankParaTable(const ParaTable& paras) {  paras_ = paras.BuildBlank(); }
  void BuildCopyParaTable(const ParaTable& paras) { paras_ = paras.BuildCopy(); }
};

}