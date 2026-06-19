#pragma once

#include <sched.h>
#include <xmmintrin.h>

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>

template <std::unsigned_integral T>
class SequenceLock;

template <std::unsigned_integral T>
class OptimisticLock;

template <std::unsigned_integral T>
class ExclusiveLock;

namespace seqlock_util {

inline void seqlock_yield(const std::size_t count) {
  if (count < 30) {
    _mm_pause();
  } else {
    sched_yield();
  }
}

template <std::unsigned_integral T>
bool is_unlocked_state(const T version) {
  return version % 2 == 0;
}

template <std::unsigned_integral T>
bool is_locked_state(const T version) {
  return version % 2 == 1;
}

}  // namespace seqlock_util

template <std::unsigned_integral T>
class LockBase {
 protected:
  SequenceLock<T> *seqlock = nullptr;

  ~LockBase() = default;
};

template <std::unsigned_integral T>
class ExclusiveLock : public LockBase<T> {
  friend class SequenceLock<T>;
  friend class OptimisticLock<T>;

 public:
  ExclusiveLock() = default;

  ~ExclusiveLock() { unlock(); }

  ExclusiveLock(const ExclusiveLock &)            = delete;
  ExclusiveLock &operator=(const ExclusiveLock &) = delete;

  ExclusiveLock(ExclusiveLock &&other) noexcept {
    assert(!other.seqlock || other.seqlock->is_locked_exclusive());
    this->seqlock = other.seqlock;
    other.seqlock = nullptr;
  }

  ExclusiveLock &operator=(ExclusiveLock &&other) noexcept {
    if (this != &other) {
      assert(this->seqlock == nullptr);
      assert(!other.seqlock || other.seqlock->is_locked_exclusive());

      this->seqlock = other.seqlock;
      other.seqlock = nullptr;
    }
    return *this;
  }

  void unlock() {
    if (!this->seqlock) {
      return;
    }

    assert(this->seqlock->is_locked_exclusive());
    this->seqlock->version.fetch_add(1, std::memory_order::release);
    this->seqlock = nullptr;
  }
};

template <std::unsigned_integral T>
class OptimisticLock : public LockBase<T> {
  friend class SequenceLock<T>;

  T lockedVersion;

 public:
  OptimisticLock() : lockedVersion(0) { this->seqlock = nullptr; }

  OptimisticLock(const OptimisticLock &)            = delete;
  OptimisticLock &operator=(const OptimisticLock &) = delete;

  OptimisticLock(OptimisticLock &&other) noexcept {
    this->lockedVersion = other.lockedVersion;
    this->seqlock       = other.seqlock;
    other.seqlock       = nullptr;
  }

  OptimisticLock &operator=(OptimisticLock &&other) noexcept {
    if (this != &other) {
      this->lockedVersion = other.lockedVersion;
      this->seqlock       = other.seqlock;
      other.seqlock       = nullptr;
    }
    return *this;
  }

  bool try_upgrade_to_exclusive(ExclusiveLock<T> &lock) {
    assert(this->seqlock);
    assert(lock.seqlock == nullptr && "Lock must be unlocked before being assigned");
    assert(seqlock_util::is_unlocked_state(lockedVersion));

    auto expVersion = lockedVersion;

    if (!this->seqlock->version.compare_exchange_strong(expVersion, expVersion + 1, std::memory_order::acquire,
                                                        std::memory_order::relaxed)) {
      return false;
    }

    lock.seqlock  = this->seqlock;
    this->seqlock = nullptr;
    return true;
  }

  [[nodiscard]] bool validate() const {
    assert(this->seqlock);
    assert(seqlock_util::is_unlocked_state(lockedVersion));
    // Prevent prior protected-data reads from sinking below this version load.
    // "release" is not a valid order for a load; an acquire fence + relaxed
    // load is the canonical seqlock reader re-check.
    std::atomic_thread_fence(std::memory_order::acquire);
    return this->seqlock->get_version(std::memory_order::relaxed) == lockedVersion;
  }
};

template <std::unsigned_integral T>
class SequenceLock {
  friend class OptimisticLock<T>;
  friend class ExclusiveLock<T>;

 private:
  std::atomic<T> version = 0;

 public:
  [[nodiscard]] T get_version(const std::memory_order memory_order = std::memory_order::acquire) const {
    return version.load(memory_order);
  }

  [[nodiscard]] bool is_locked_exclusive(const std::memory_order memory_order = std::memory_order::acquire) const {
    return version.load(memory_order) % 2 == 1;
  }

  [[nodiscard]] bool is_unlocked(const std::memory_order memory_order = std::memory_order::acquire) const {
    return version.load(memory_order) % 2 == 0;
  }

  bool try_lock_optimistically(OptimisticLock<T> &lock) {
    assert(lock.seqlock == nullptr && "lock must be unlocked before being assigned");

    const auto opt_version = get_version(std::memory_order::acquire);

    if (seqlock_util::is_locked_state(opt_version)) {
      return false;
    }

    lock.lockedVersion = opt_version;
    lock.seqlock       = this;
    return true;
  }

  void lock_optimistically(OptimisticLock<T> &lock) {
    assert(lock.seqlock == nullptr && "lock must be unlocked before being assigned");

    std::size_t i    = 0;
    auto opt_version = get_version(std::memory_order::relaxed);

    while (seqlock_util::is_locked_state(opt_version)) {
      seqlock_util::seqlock_yield(i++);
      opt_version = get_version(std::memory_order::relaxed);
    }

    std::atomic_thread_fence(std::memory_order::acquire);

    lock.lockedVersion = opt_version;
    lock.seqlock       = this;
  }

  bool try_lock_exclusive(ExclusiveLock<T> &lock) {
    assert(lock.seqlock == nullptr && "lock must be unlocked before being assigned");

    auto lock_state = get_version(std::memory_order::relaxed);

    if (!seqlock_util::is_unlocked_state(lock_state) ||
        !this->version.compare_exchange_strong(lock_state, lock_state + 1, std::memory_order::acquire,
                                               std::memory_order::relaxed)) {
      return false;
    }

    assert(is_locked_exclusive());
    lock.seqlock = this;
    return true;
  }

  void lock_exclusive(ExclusiveLock<T> &lock) {
    assert(lock.seqlock == nullptr && "lock must be unlocked before being assigned");

    std::size_t i   = 0;
    auto lock_state = get_version(std::memory_order::relaxed);

    while (!seqlock_util::is_unlocked_state(lock_state) ||
           !this->version.compare_exchange_weak(lock_state, lock_state + 1, std::memory_order::acquire,
                                                std::memory_order::relaxed)) {
      seqlock_util::seqlock_yield(i++);
      lock_state = get_version(std::memory_order::relaxed);
    }

    assert(is_locked_exclusive());
    lock.seqlock = this;
  }

  OptimisticLock<T> lock_optimistically() {
    OptimisticLock<T> lock;
    lock_optimistically(lock);
    return lock;
  }

  ExclusiveLock<T> lock_exclusive() {
    ExclusiveLock<T> lock;
    lock_exclusive(lock);
    return lock;
  }
};
