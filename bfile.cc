#include "bfile.h"
#include "db/version_set.h"

namespace sagitrs {

leveldb::Iterator* BFile::NewIterator(const ReadOptions& roptions, leveldb::Version* version) {
  return version->GetBFileIterator(roptions, file_meta_->number, file_meta_->file_size);
}
}