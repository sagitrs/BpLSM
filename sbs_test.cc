// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "gtest/gtest.h"
#include "sbs.h"
#include "bfile.h"
#include "bounded.h"
#include "bounded_value_container.h"


namespace sagitrs {

BFile* BuildFile(size_t a, size_t b) {
  static sagitrs::SBSOptions options;
  Statistics stats(options, options.NowTimeSlice());
  std::string l = std::to_string(a);
  std::string r = std::to_string(b);
  uint64_t v = a * 100 + b;
  leveldb::FileMetaData * f = new leveldb::FileMetaData();
  f->number = v;
  f->smallest = leveldb::InternalKey(l, 0, leveldb::ValueType::kTypeValue);
  f->largest = leveldb::InternalKey(r, 0, leveldb::ValueType::kTypeValue);
  return new BFile(f, stats);
}

TEST(SBSTest, Empty) {}

TEST(SBSTest, Simple) {
  sagitrs::SBSOptions options;
  sagitrs::SBSkiplist list(options);
  for (size_t i =9; i >= 1; i --) {
    list.Put(BuildFile(i*10+0, i*10+0));
    //std::cout << list.ToString() << std::endl;
    list.Put(BuildFile(i*10+9, i*10+9));
    //std::cout << list.ToString() << std::endl;
  }
  for (size_t i = 1; i <= 9; ++i) {
    list.Put(BuildFile(i*10+0, i*10+9));
    //std::cout << list.ToString() << std::endl;
  }
  RealBounded bound("70", "70");
  sagitrs::BFileVec container[3];
  list.Lookup(bound, container[0]);

  ASSERT_EQ(container[0].size(), 2);
  //---------------------------------------
  for (size_t i = 1; i <= 8; ++i) {
    list.Put(BuildFile(60+i, 60+i));
    list.Put(BuildFile(70+i, 70+i));
    //std::cout << list.ToString() << std::endl;
  }
  //std::cout << list.ToString() << std::endl;
  //return;
  BFile* d = nullptr;
  d = list.Pop(*BuildFile(50, 59));
  //std::cout << list.ToString() << std::endl;
  // i know there is memory leak here...
  d = list.Pop(*BuildFile(60, 69));
  //std::cout << list.ToString() << std::endl;
  //---------------------------------------
  d = list.Pop(*BuildFile(29, 29));
  //std::cout << list.ToString() << std::endl;
  d = list.Pop(*BuildFile(20, 20));
  //std::cout << list.ToString() << std::endl;
  //---------------------------------------
  d = list.Pop(*BuildFile(20, 29));
  std::cout << list.ToString() << std::endl;
}

}  // namespace leveldb

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
