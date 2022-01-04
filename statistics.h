#pragma once

#include <array>
#include "options.h"
#include "counter.h"

namespace sagitrs {

struct Statistics {
  std::vector<Counter> list_;
  typedef std::shared_ptr<Counter> CPTR;
 private:
  std::shared_ptr<StatisticsOptions> options_;
  std::deque<CPTR> history_;
};

}