#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "tbb/enumerable_thread_specific.h"
#include "tiny_ptr/sequence_lock.h"
#include "tiny_ptr/tiny_ptr.h"

// Bucket indexing: prime modulo (1) or power-of-two mask (0).
// Overridable from the build system, e.g. -DUSE_PRIMES=0.
#ifndef USE_PRIMES
#define USE_PRIMES 1
#endif

#include "util/primes.hpp"

/**
 * Turns a hash value into a bucket index, and owns the bucket count that goes
 * with the chosen reduction. Sole purpose is to keep the prime/power-of-two
 * choice in one place: DerefTable holds one of these and never mentions
 * USE_PRIMES itself.
 *
 * Prime modulo consumes all 64 bits of the hash; the mask consumes the low
 * bits, which is what makes the low-bit injectivity of id_hash meaningful (see
 * src/tiny_ptr/util.h) at the cost of rounding the table up to a power of two.
 */
class BucketIndexer {
#if USE_PRIMES
  // See:
  // https://databasearchitects.blogspot.com/2020/01/all-hash-table-sizes-you-will-ever-need.html
  primes::Prime _prime;

 public:
  BucketIndexer() = default;

  explicit BucketIndexer(const uint32_t requested_buckets)
      : _prime{primes::Prime::pick(requested_buckets)} {}

  [[nodiscard]] uint32_t buckets() const {
    return static_cast<uint32_t>(_prime.get());
  }

  [[nodiscard]] size_t operator()(const uint64_t hash) const {
    return _prime.mod(hash);
  }
#else
  size_t _mask = 0;

 public:
  BucketIndexer() = default;

  // bit_ceil widens to uint64_t to avoid overflow UB and maps a request of 0 to
  // a single bucket.
  explicit BucketIndexer(const uint32_t requested_buckets)
      : _mask(std::bit_ceil(static_cast<uint64_t>(requested_buckets)) - 1) {}

  [[nodiscard]] uint32_t buckets() const {
    return static_cast<uint32_t>(_mask + 1);
  }

  [[nodiscard]] size_t operator()(const uint64_t hash) const {
    return hash & _mask;
  }
#endif
};

// TODO: These values could be a little more conservative
//  Do some experimental testing.
consteval double load_factor(size_t bin_size) {
  if (bin_size >= (1 << 8) - 1) {
    return 1.0;
  }
  if (bin_size >= (1 << 7) - 1) {
    return 0.95;
  }
  if (bin_size >= (1 << 6) - 1) {
    return 0.90;
  }
  if (bin_size >= (1 << 5) - 1) {
    return 0.82;
  }
  if (bin_size >= (1 << 4) - 1) {
    return 0.70;
  }
  if (bin_size >= (1 << 3) - 1) {
    return 0.45;
  }

  throw "bin size is too small";
}

/**
 * The smallest unsigned integer type able to hold NBits bits.
 */
template <unsigned NBits>
  requires(NBits <= 64)
using smallest_uint_t = std::conditional_t<
    NBits <= 8, uint8_t,
    std::conditional_t<NBits <= 16, uint16_t,
                       std::conditional_t<NBits <= 32, uint32_t, uint64_t>>>;

/**
 * @class DerefTable
 * @brief A thread-safe dereference table implementation following the
 * power-of-two-choice scheme as presented in the "Succinct and Fast Tiny
 * Pointer Hash Tables" paper.
 *
 * @tparam TObject The type of objects stored within the dereference table.
 */
template <typename TObject, std::unsigned_integral TTinyPtrT = uint8_t,
          unsigned STinyPtr = 0>
class DerefTable {
  static_assert(std::is_trivially_destructible_v<TObject>,
                "DerefTable requires trivially destructible TObject because "
                "entries are managed as raw storage");

  // The bits of a tiny pointer left for the entry index, i.e., all bits except
  // the hash bit and the STinyPtr special bits.
  static constexpr unsigned BUCKET_INDEX_BITS =
      sizeof(TTinyPtrT) * 8 - 1 - STinyPtr;

  using SequenceLockT = uint8_t;

 public:
  // Entry indices only ever use BUCKET_INDEX_BITS bits, so for large STinyPtr
  // values this can be narrower than TTinyPtrT.
  using BucketIndexT = smallest_uint_t<BUCKET_INDEX_BITS>;
  using TinyPtrT = TinyPtr<TTinyPtrT, STinyPtr>;
  using ObjectT = TObject;

  static_assert(sizeof(TObject) >= sizeof(BucketIndexT),
                "DerefTable requires TObject to be at least as large as "
                "BucketIndexT to store the free slot index in its storage");

  // minus 1 to preserve null and tagged tinyptr
  static constexpr size_t ENTRIES_PER_BIN_COUNT =
      (size_t{1} << BUCKET_INDEX_BITS) - 1;

  // If newly "allocated" memory should be zeroed before it is returned.
  static constexpr bool ZERO_NEW_ALLOCATED_MEMORY = false;

 private:
  struct SizeCounters {
    int64_t size = 0;
    int64_t max_reached_size = 0;
  };

  // Maps a hash value to a bucket index. Declared before _num_buckets, which is
  // derived from it.
  BucketIndexer _indexer;

  // Statistic counters.
  // Counters can be negative if a specific thread deleted more objects than he
  // created.
  tbb::enumerable_thread_specific<SizeCounters,
                                  tbb::cache_aligned_allocator<SizeCounters>,
                                  tbb::ets_key_per_instance>
      thread_counters;
  // The number of buckets of this dereference table.
  uint32_t _num_buckets;
  // The max capacity of the dereference table e.g., how many objects of type T
  // can be stored at most.
  uint32_t _capacity;

  // avoid false sharing by having each entry in its own cache-line
  // TODO: Do we really need this? Maybe pack more tightly with 8 bit sequence
  //  lock for better cache utilization? benchmark...
  struct /*alignas(64)*/ MetaTableEntry {
    SequenceLock<SequenceLockT> lock;
    std::atomic<BucketIndexT> free_slot_count = 0;
    std::atomic<BucketIndexT> free_slot_index = 0;
  };

  struct DataTableEntry {
    struct EntryStorage {
      alignas(TObject) std::byte bytes[sizeof(TObject)];
    };

    std::array<EntryStorage, ENTRIES_PER_BIN_COUNT> entries;

    // non-default empty constructor to leave entries uninitialized until it is
    // overwritten by the free list.
    DataTableEntry() {}  // NOLINT(*-use-equals-default)

    [[nodiscard]] BucketIndexT get_next_free_slot_index(
        size_t entry_index) const {
      return *reinterpret_cast<const BucketIndexT*>(&entries[entry_index]);
    }

    void set_next_free_slot_index(size_t entry_index,
                                  const BucketIndexT next_free_entry_index) {
      *reinterpret_cast<BucketIndexT*>(&entries[entry_index]) =
          next_free_entry_index;
    }
  };

  // Meta-Table storing per bucket the number of free slots and the index of the
  // head of the free list
  std::vector<MetaTableEntry> meta_table;
  // Data-Table storing per bucket the array of bucket entries of type T. Each
  // entry represents either an object of type T or uses the first byte of its
  // memory to store its node in the linked-free-list.
  std::vector<DataTableEntry> data_table;

  void increment_size() {
    auto& counters = thread_counters.local();
    ++counters.size;
    if (counters.size > counters.max_reached_size) {
      counters.max_reached_size = counters.size;
    }
  }

  void decrement_size() { --thread_counters.local().size; }

 public:
  DerefTable() : _num_buckets(0), _capacity(0) {}

  explicit DerefTable(uint32_t ht_bucket_count);

  DerefTable(const DerefTable&) = delete;

  DerefTable& operator=(const DerefTable&) = delete;

  DerefTable(DerefTable&& other) noexcept
      : _indexer(other._indexer),
        thread_counters(std::move(other.thread_counters)),
        _num_buckets(other._num_buckets),
        _capacity(other._capacity),
        meta_table(std::move(other.meta_table)),
        data_table(std::move(other.data_table)) {
    other._num_buckets = 0;
    other._capacity = 0;
  }

  DerefTable& operator=(DerefTable&& other) noexcept {
    if (this != &other) {
      this->~DerefTable();
      new (this) DerefTable(std::move(other));
    }
    return *this;
  }

  static DerefTable Create(size_t max_capacity);

  /**
   * Returns the number of allocated objects. Call only after all concurrent
   * allocations and frees have completed.
   */
  [[nodiscard]] uint32_t size() const {
    int64_t result = 0;
    for (const auto& counters : thread_counters) {
      result += counters.size;
    }
    assert(result >= 0 && static_cast<uint64_t>(result) <= _capacity);
    return static_cast<uint32_t>(result);
  }

  /**
   * Returns an upper bound on the maximum number of simultaneously allocated
   * objects. Call only after all concurrent allocations and frees have
   * completed. It is exact while the table grows monotonically; after frees,
   * thread-local peaks may have occurred at different times.
   */
  [[nodiscard]] uint64_t max_reached_size() const {
    uint64_t result = 0;
    for (const auto& counters : thread_counters) {
      result += static_cast<uint64_t>(counters.max_reached_size);
    }
    return result;
  }

  [[nodiscard]] uint32_t capacity() const { return _capacity; }

  /**
   * Tries to allocate a new object inside this dereference table, returning a
   * tinyptr_t and a pointer to the newly allocated object. Aborts if there was
   * no more free space for allocating a new object.
   *
   * This operation is thread-safe.
   *
   * @param h The hash values associated with the tiny pointer.
   * @param special An optional special value to be stored in the tinyptr_t
   * (e.g., for pointer tagging).
   * @return A pair of a tinyptr_t and a pointer to the newly allocated object.
   */
  std::pair<TinyPtrT, TObject*> allocate(TinyPtrHashes h,
                                         TTinyPtrT special = 0) {
    return allocate_impl(h, special, ZERO_NEW_ALLOCATED_MEMORY);
  }

  /**
   * "Frees" the object pointed to by the given tinyptr_t and the hash values.
   * Note that this memory is not directly returned to the operating system but
   * instead stays allocated for possible future object allocations.
   *
   * The memory of the dereference table is only freed upon its deconstruction.
   *
   * This operation is thread-safe.
   *
   * @param tinyptr The tiny pointer to free.
   * @param h The hash values associated with the tiny pointer.
   */
  void free(TinyPtrT tinyptr, TinyPtrHashes h);

  /**
   * "Dereferences" a given tiny pointer and returns a pointer to the object
   * that it points to.
   * @param tinyptr The tiny pointer.
   * @param h The hash values associated with the tiny pointer.
   * @return A pointer to the object pointed at by the tiny pointer.
   */
  TObject* dereference(TinyPtrT tinyptr, TinyPtrHashes h);

  /**
   * "Dereferences" a given tiny pointer and returns a pointer to the object
   * that it points to.
   * @param tinyptr The tiny pointer.
   * @param h The hash values associated with the tiny pointer.
   * @return A pointer to the object pointed at by the tiny pointer.
   */
  const TObject* dereference(TinyPtrT tinyptr, TinyPtrHashes h) const;

  void printDerefTableStats() const;

 private:
  std::pair<TinyPtrT, TObject*> allocate_impl(TinyPtrHashes h,
                                              TTinyPtrT special,
                                              bool zero_memory);

  TObject* get_data_object(size_t data_table_index, size_t object_index);

  const TObject* get_data_object(size_t data_table_index,
                                 size_t object_index) const;
};

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
DerefTable<TObject, TTinyPtr, STinyPtr>::DerefTable(
    const uint32_t ht_bucket_count)
    : _indexer(ht_bucket_count),
      _num_buckets(_indexer.buckets()),
      _capacity(_num_buckets * ENTRIES_PER_BIN_COUNT),
      meta_table(_num_buckets),
      data_table(_num_buckets) {
  for (auto& meta_data : meta_table) {
    meta_data.free_slot_count = ENTRIES_PER_BIN_COUNT;
  }

  for (auto& data_entry : data_table) {
    if (ZERO_NEW_ALLOCATED_MEMORY) {
      memset(data_entry.entries.data(), 0, sizeof(data_entry.entries));
    }
    for (int i = 0; i < data_entry.entries.size() - 1; ++i) {
      data_entry.set_next_free_slot_index(i, i + 1);
    }
    data_entry.set_next_free_slot_index(data_entry.entries.size() - 1, 0);
  }
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
DerefTable<TObject, TTinyPtr, STinyPtr>
DerefTable<TObject, TTinyPtr, STinyPtr>::Create(const size_t max_capacity) {
  const auto min_bucket_count = static_cast<uint32_t>(
      (max_capacity + ENTRIES_PER_BIN_COUNT - 1) / ENTRIES_PER_BIN_COUNT);
  // divide the minimum bucket count by the expected load factor estimate
  return DerefTable(
      static_cast<uint32_t>(static_cast<double>(min_bucket_count) /
                            load_factor(ENTRIES_PER_BIN_COUNT)));
}

inline void abort_overflow(uint64_t size, uint32_t capacity) {
  throw std::runtime_error(std::format(
      "Unable to allocate new object at fill factor {}. Size: {}, Capacity: "
      "{}\n"
      "If this already throws at a low fill factor either"
      " a) increase the tiny pointer size to have more objects per bin"
      " b) improve the value distribution of your hash functions"
      " c) improve the distribution of the ID that used as input for the hash "
      "functions",
      static_cast<double>(size) / static_cast<double>(capacity), size,
      capacity));
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
std::pair<typename DerefTable<TObject, TTinyPtr, STinyPtr>::TinyPtrT, TObject*>
DerefTable<TObject, TTinyPtr, STinyPtr>::allocate_impl(const TinyPtrHashes h,
                                                       const TTinyPtr special,
                                                       bool zero_memory) {
  auto h1_index = _indexer(h.first);
  auto h2_index = _indexer(h.second);

  auto& h1_meta_data = meta_table[h1_index];
  auto& h2_meta_data = meta_table[h2_index];

  ExclusiveLock<SequenceLockT> h_excl_lock;

retry: {
  auto h1_opt_lock = h1_meta_data.lock.lock_optimistically();
  auto h2_opt_lock = h2_meta_data.lock.lock_optimistically();

  const TTinyPtr h1_free_count =
      h1_meta_data.free_slot_count.load(std::memory_order::relaxed);
  const TTinyPtr h2_free_count =
      h2_meta_data.free_slot_count.load(std::memory_order::relaxed);

  uint8_t h_bit = h1_free_count >= h2_free_count ? 0 : 1;
  auto h_index = h_bit ? h2_index : h1_index;
  auto& h_meta_data = h_bit ? h2_meta_data : h1_meta_data;
  auto& h_meta_data_lock = h_bit ? h2_opt_lock : h1_opt_lock;
  auto& other_meta_data_lock = h_bit ? h1_opt_lock : h2_opt_lock;

  const TTinyPtr object_index =
      h_meta_data.free_slot_index.load(std::memory_order::relaxed);
  auto& object_entry = *get_data_object(h_index, object_index);
  const TTinyPtr next_free_slot_index =
      data_table[h_index].get_next_free_slot_index(object_index);

  // Note: for the case where h1 = h2 we have to make sure we validate the
  // optimistic lock before upgrading
  if (!other_meta_data_lock.validate() ||
      !h_meta_data_lock.try_upgrade_to_exclusive(h_excl_lock)) {
    goto retry;
  }

  if (h_meta_data.free_slot_count == 0) {
    abort_overflow(size(), _capacity);
  }

  assert(object_index < ENTRIES_PER_BIN_COUNT &&
         "used free_slot_index out of bounds");

  h_meta_data.free_slot_count.fetch_sub(1, std::memory_order::relaxed);
  h_meta_data.free_slot_index.store(next_free_slot_index,
                                    std::memory_order::relaxed);

  h_excl_lock.unlock();

  increment_size();

  if (zero_memory) {
    memset(&object_entry, 0, sizeof(TObject));
  }

  return {TinyPtrT(object_index, special, h_bit), &object_entry};
}
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
void DerefTable<TObject, TTinyPtr, STinyPtr>::free(const TinyPtrT tinyptr,
                                                   const TinyPtrHashes h) {
  const uint64_t hash = tinyptr.hash_fn() ? h.second : h.first;
  TTinyPtr index = tinyptr.index();

  const auto h_index = _indexer(hash);

  auto& h_meta_data = meta_table[h_index];

  auto excl_lock = h_meta_data.lock.lock_exclusive();

  data_table[h_index].set_next_free_slot_index(
      index, h_meta_data.free_slot_index.load(std::memory_order::relaxed));
  h_meta_data.free_slot_index.store(index, std::memory_order::relaxed);
  h_meta_data.free_slot_count.fetch_add(1, std::memory_order::relaxed);

  excl_lock.unlock();

  decrement_size();
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
TObject* DerefTable<TObject, TTinyPtr, STinyPtr>::dereference(
    const TinyPtrT tinyptr, const TinyPtrHashes h) {
  return const_cast<TObject*>(
      static_cast<const DerefTable*>(this)->dereference(tinyptr, h));
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
const TObject* DerefTable<TObject, TTinyPtr, STinyPtr>::dereference(
    const TinyPtrT tinyptr, const TinyPtrHashes h) const {
  if (tinyptr == TinyPtrT::null) {
    return nullptr;
  }
  if (tinyptr == TinyPtrT::tagged) {
    throw std::runtime_error("Cannot dereference tagged tinyptr");
  }

  const uint64_t hash = tinyptr.hash_fn() ? h.second : h.first;
  const TTinyPtr index = tinyptr.index();

  return get_data_object(_indexer(hash), index);
}

template <typename TObject, std::unsigned_integral TTinyPtrT, unsigned STinyPtr>
void DerefTable<TObject, TTinyPtrT, STinyPtr>::printDerefTableStats() const {
  auto s = size();
  auto max_size = max_reached_size();
  std::cout << "Size: " << s << ", Max Size Upper Bound: " << max_size
            << ", Capacity: " << capacity() << ", Fill Factor: "
            << static_cast<double>(s) / static_cast<double>(capacity())
            << ", Max Fill Factor Upper Bound: "
            << static_cast<double>(max_size) / static_cast<double>(capacity())
            << std::endl;
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
TObject* DerefTable<TObject, TTinyPtr, STinyPtr>::get_data_object(
    const size_t data_table_index, const size_t object_index) {
  return const_cast<TObject*>(
      static_cast<const DerefTable*>(this)->get_data_object(data_table_index,
                                                            object_index));
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
const TObject* DerefTable<TObject, TTinyPtr, STinyPtr>::get_data_object(
    size_t data_table_index, size_t object_index) const {
  // convert TinyPtr to object pointer without any memory lookups!
  return reinterpret_cast<const TObject*>(
      &data_table[data_table_index].entries[object_index]);
}