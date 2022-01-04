// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "gtest/gtest.h"
#include "sbs.h"
#include "bounded.h"
#include "bounded_value_container.h"

namespace leveldb {

TEST(SBSTest, Empty) {}
struct TempKV : virtual public sagitrs::BoundedValue,
            virtual public sagitrs::BRealBounded {
  uint64_t value_;
  TempKV(const Slice& a, const Slice& b, uint64_t value) 
  : BRealBounded(a, b), value_(value) {}
  virtual uint64_t Identifier() const override { return value_; }

  static std::shared_ptr<TempKV> FactoryBuild(size_t a, size_t b) {
    std::string l = std::to_string(a);
    std::string r = std::to_string(b);
    uint64_t v = a * 100 + b;
    return std::make_shared<TempKV>(l, r, v);
  }
};

TEST(SBSTest, Simple) {
  sagitrs::SBSkiplist list;
  for (size_t i =9; i >= 1; i --) {
    list.Put(TempKV::FactoryBuild(i*10+0, i*10+0));
    //std::cout << list.ToString() << std::endl;
    list.Put(TempKV::FactoryBuild(i*10+9, i*10+9));
    //std::cout << list.ToString() << std::endl;
  }
  std::cout << list.ToString() << std::endl;
  for (size_t i = 1; i <= 9; ++i) {
    list.Put(TempKV::FactoryBuild(i*10+0, i*10+9));
    //std::cout << list.ToString() << std::endl;
  }
  
  std::cout << list.ToString() << std::endl;
  
  Slice target_key("70");
  TempKV kv(target_key, target_key, 12345678);
  sagitrs::BoundedValueContainer container;
  list.Get(kv, container);

  ASSERT_EQ(container.size(), 3);
}

}  // namespace leveldb

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
