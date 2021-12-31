#pragma once
#include <mutex>

namespace sagitrs {

struct Lockable {
  Lockable() = default;
  virtual ~Lockable() {}

  Lockable(const Lockable&) = delete;
  Lockable& operator=(const Lockable&) = delete;

  void ReadLock() { mu_.lock(); }
  void WriteLock() { mu_.lock(); }
  void Unlock() { mu_.unlock(); }
  void AssertHeld() {}
 private:
  std::mutex mu_;
};

struct LockGuard {
 enum LockType { ReadLock, WriteLock };
  private:
  Lockable *lock_;
  public:
  explicit LockGuard(Lockable* lock, LockType type) : lock_(lock) { 
    if (type == WriteLock) 
      lock_->WriteLock();
    else if (type == ReadLock)
      lock_->ReadLock(); 
  }
  ~LockGuard() { 
    lock_->Unlock(); 
  }
};

}