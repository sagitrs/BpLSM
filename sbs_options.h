#pragma once

#include <vector>
#include <stack>

namespace sagitrs {

struct SBSOptions {
  size_t width_[3] = {2, 4, 8};
  size_t MinWidth() const { return width_[0]; }
  size_t DefaultWidth() const { return width_[1]; }
  size_t MaxWidth() const { return width_[2]; }

  int TestState(size_t size, bool lbound_ignore) const {
    if (!lbound_ignore && size < MinWidth()) return -1;
    if (size > MaxWidth()) return 1;
    return 0;
  }
};

}