// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "gtest/gtest.h"
#include "sbs.h"
#include "bounded.h"

namespace leveldb {

TEST(SBSTest, Empty) {}
struct TempKV : virtual public sagitrs::BoundedValue,
            virtual public sagitrs::BRealBounded {
  uint64_t value_;
  TempKV(const Slice& a, const Slice& b, uint64_t value) 
  : BRealBounded(a, b), value_(value) {}
  virtual uint64_t Identifier() const override { return value_; }
};

TEST(SBSTest, Simple) {
  std::vector<std::string> alpha, alpha_min, alpha_max;
  std::vector<std::shared_ptr<TempKV>> set1, set2;
  sagitrs::SBSkiplist<TempKV> list;
  {
    for (char c = '0'; c <= '9'; ++c) 
      alpha.emplace_back(1, c);
    for (auto s : alpha) {
      alpha_min.emplace_back(s + '0');
      alpha_max.emplace_back(s + '9');
      set1.push_back(std::make_shared<TempKV>(s, s, alpha_min.size() * 1));
      set2.push_back(std::make_shared<TempKV>(*alpha_min.rbegin(), *alpha_max.rbegin(), alpha_min.size() * 11));
      list.Put(*set1.rbegin());
    }  
    std::cout << list.ToString() << std::endl;
  }
  

  ASSERT_EQ(true, true);
}

}  // namespace leveldb

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
