#pragma once
#include <mutex>
#include <shared_mutex>
//#define LOCK_TYPE_MUTEX
#define LOCK_TYPE_PTHREAD_RWLOCK
//#define LOCK_TYPE_SHARED_MUTEX

namespace sagitrs {

struct Lockable {
#if defined(LOCK_TYPE_MUTEX)
 private:
  std::mutex mu_;
 public:
  Lockable() = default;
  virtual ~Lockable() {}
  void ReadLock() { mu_.lock(); }
  void WriteLock() { mu_.lock(); }
  void Unlock() { mu_.unlock(); }
  void AssertHeld() {}
#elif defined(LOCK_TYPE_PTHREAD_RWLOCK)
 private:
  pthread_rwlock_t lock_;
 public:
  Lockable() : lock_() { pthread_rwlock_init(&lock_, nullptr); }
  virtual ~Lockable() { pthread_rwlock_destroy(&lock_); }
  void ReadLock() { pthread_rwlock_rdlock(&lock_); }
  void WriteLock() { pthread_rwlock_wrlock(&lock_); }
  void Unlock() { pthread_rwlock_unlock(&lock_); }
  void AssertHeld() {}
#elif defined(LOCK_TYPE_SHARED_MUTEX)
#else
  void ReadLock() { mu_.lock(); }
  void WriteLock() { mu_.lock(); }
  void Unlock() { mu_.unlock(); }
  void AssertHeld() {}
 private:
  std::mutex mu_;
#endif
  Lockable(const Lockable&) = delete;
  Lockable& operator=(const Lockable&) = delete;
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