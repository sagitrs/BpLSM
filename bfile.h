#pragma once

#include "bounded.h"
#include "statistics.h"
#include "leveldb/options.h"
#include "../../db/version_edit.h"
#include "../../db/version_set.h"
#include "leveldb/iterator.h"

namespace sagitrs {

struct BFile : virtual public Bounded, virtual public Identifiable, 
               public Statistics {
  enum BFileType { TypeTape, TypeHole };
 private:
  int deleted_level_;
  BFileType type_;
  leveldb::FileMetaData* file_meta_;
 public:
  // for deletion only.
  BFile(leveldb::FileMetaData* f)
  : Statistics(),
    deleted_level_(-1), type_(TypeHole),
    file_meta_(f) { f->refs++; } 
  BFile(leveldb::FileMetaData* f, const Statistics& init) 
  : Statistics(init), 
    deleted_level_(-1), type_(TypeHole),
    file_meta_(f) { f->refs++; }
  virtual ~BFile() {
    if (file_meta_ && --file_meta_->refs <= 0)
      delete file_meta_;
  }
  virtual Slice Min() const override { return file_meta_->smallest.user_key(); }
  virtual Slice Max() const override { return file_meta_->largest.user_key(); }
  virtual uint64_t Identifier() const override { return file_meta_->number; }
  virtual uint64_t Size() const override { return file_meta_->file_size; } 
  virtual void* Value() const override { return file_meta_; }

  leveldb::FileMetaData* Data() const { return file_meta_; }

  void SetDeletedLevel(int l) { deleted_level_ = l; }
  int DeletedLevel() const { return deleted_level_; }
  void SetType(BFileType type) { type_ = type; }
  BFileType Type() const { return type_; }

  using Statistics::UpdateTime;
  using Statistics::UpdateStatistics;
  using Statistics::GetStatistics;
  using Statistics::CopyStatistics;
  using Statistics::MergeStatistics;

  virtual void GetStringSnapshot(std::vector<Printable::KVPair>& snapshot) const override {
    snapshot.emplace_back("NUM", std::to_string(Identifier()));
    Statistics::GetStringSnapshot(snapshot);
  }

  leveldb::Iterator* NewIterator(const leveldb::ReadOptions& roptions, leveldb::Version* version);
};

}
