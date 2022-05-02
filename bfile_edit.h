#pragma once

#include "../../db/version_edit.h"
#include "bfile.h"
#include <memory>
#include <sstream>

namespace sagitrs {

struct BFileEdit {
  std::vector<leveldb::FileMetaData*> deleted_;
  std::vector<leveldb::FileMetaData*> generated_;
 public:
  BFileEdit() : deleted_(), generated_() {}
  //explicit BFileEdit(const VersionEdit& edit, Version* base);
  uint64_t Hash() const {
    uint64_t hash = 0;
    for (auto& file : deleted_) 
      hash += file->number;
    //assert(generated_.size() <= 1 || hash > 0);
    return hash;
  }
  void Add(leveldb::FileMetaData* f) { generated_.push_back(f); }
  void Del(leveldb::FileMetaData* f) { deleted_.push_back(f); }
  void Dels(const std::vector<leveldb::FileMetaData*>& files) { 
    for (auto file : files) 
      Del(file); 
  }
  std::string ToString() const {
    std::stringstream ss;
    ss << "BFileEdit={";
    for (auto file : deleted_) {
      ss << "-" << file->number << ",";
    }
    for (auto file : generated_) {
      ss << "+" << file->number << ",";
    }
    ss << "}";
    return ss.str();
  }
};

}
