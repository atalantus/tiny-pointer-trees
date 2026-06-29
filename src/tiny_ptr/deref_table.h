#pragma once

#include <cassert>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <stdexcept>
#include <vector>

#include "tiny_ptr/sequence_lock.h"
#include "tiny_ptr/tiny_ptr.h"

#include "util/primes.hpp"

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
 * @class DerefTable
 * @brief A thread-safe dereference table implementation following the power-of-two-choice scheme as
 * presented in the "Succinct and Fast Tiny Pointer Hash Tables" paper.
 *
 * @tparam TObject The type of objects stored within the dereference table.
 */
template <typename TObject, std::unsigned_integral TTinyPtrT = uint8_t, unsigned
          STinyPtr = 0>
class DerefTable {
  static_assert(sizeof(TObject) >= 1,
                "DerefTable requires T to be at least 1 byte in size to store free slot index")
  ;
  static_assert(std::is_trivially_destructible_v<TObject>,
                "DerefTable requires trivially destructible TObject because entries are managed as raw storage");

  // TODO: Technically if we have a large STinyPtr value the maximum needed type here might be smaller than
  //  TTinyPtrValue
  using BucketIndexT = TTinyPtrT;

public:
  using TinyPtrT = TinyPtr<TTinyPtrT, STinyPtr>;

  static constexpr size_t ENTRIES_PER_BIN_COUNT =
      (1 << (sizeof(TTinyPtrT) * 8 - STinyPtr - 1)) - 1;
  // (minus 1 to preserve null and tagged tinyptr)

  // If newly "allocated" memory should be zeroed before it is returned.
  static constexpr bool ZERO_NEW_ALLOCATED_MEMORY = true;

private:
  // The prime number used for the hash table. See: https://databasearchitects.blogspot.com/2020/01/all-hash-table-sizes-you-will-ever-need.html
  primes::Prime _ht_prime;
  // The current size of the dereference table e.g., how many objects of type T are currently stored.
  std::atomic<uint32_t> _size;
  // The number of buckets of this dereference table. This is always a power of two for fast bucket indexing
  // (simple binary-and operation instead of modulo).
  uint32_t _num_buckets;
  // The max capacity of the dereference table e.g., how many objects of type T can be stored at most.
  uint32_t _capacity;

  struct MetaTableEntry {
    std::atomic<BucketIndexT> free_slot_count = 0;
    std::atomic<BucketIndexT> free_slot_index = 0;
    // TODO: What about cash line ping pong? Technically whenever a lock get unlocked the meta table entry for it will
    //  will have changed anyway? Avoid for locking?
    SequenceLock<uint8_t> lock;
  };

  struct DataTableEntry {
    struct EntryStorage {
      alignas(TObject) std::byte bytes[sizeof(TObject)];
    };
    std::array<EntryStorage, ENTRIES_PER_BIN_COUNT> entries{};

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

  // Meta-Table storing per bucket the number of free slots and the index of the head of the free list
  std::vector<MetaTableEntry> meta_table;
  // Data-Table storing per bucket the array of bucket entries of type T. Each entry represents either an
  // object of type T or uses the first byte of its memory to store its node in the linked-free-list.
  std::vector<DataTableEntry> data_table;

public:
  DerefTable() : _size(0), _num_buckets(0), _capacity(0) {
  }

  explicit DerefTable(uint32_t ht_bucket_count);

  DerefTable(const DerefTable&) = delete;

  DerefTable& operator=(const DerefTable&) = delete;

  DerefTable(DerefTable&& other) noexcept
    : _ht_prime(other._ht_prime),
      _size(other._size.load(std::memory_order::relaxed)),
      _num_buckets(other._num_buckets),
      _capacity(other._capacity),
      meta_table(std::move(other.meta_table)),
      data_table(std::move(other.data_table)) {
    other._size.store(0, std::memory_order::relaxed);
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

  [[nodiscard]] uint32_t size() const {
    return _size.load(std::memory_order::relaxed);
  }

  [[nodiscard]] uint32_t capacity() const { return _capacity; }

  /**
   * Tries to allocate a new object inside this dereference table, returning a tinyptr_t
   * and a pointer to the newly allocated object. Throws a runtime error if there was no more
   * free space for allocating a new object.
   *
   * This operation is thread-safe.
   *
   * @param h A pair of hash values from the first and second hash function.
   * @param special An optional special value to be stored in the tinyptr_t (e.g., for pointer tagging).
   * @return A pair of a tinyptr_t and a pointer to the newly allocated object.
   */
  std::pair<TinyPtrT, TObject*> allocate(std::pair<uint64_t, uint64_t> h,
                                         TTinyPtrT special = 0);

  /**
   * "Frees" the object pointed to by the given tinyptr_t and the hash values.
   * Note that this memory is not directly returned to the operating system but instead
   * stays allocated for possible future object allocations.
   *
   * The memory of the dereference table is only freed upon its deconstruction.
   *
   * This operation is thread-safe.
   *
   * @param tinyptr The tiny pointer to free.
   * @param h A pair of hash values from the first and second hash function associated with the tiny pointer.
   */
  void free(TinyPtrT tinyptr, std::pair<uint64_t, uint64_t> h);

  /**
   * "Dereferences" a given tiny pointer and returns a pointer to the object that it points to.
   * @param tinyptr The tiny pointer.
   * @param h A pair of hash values from the first and second hash function associated with the tiny pointer.
   * @return A pointer to the object pointed at by the tiny pointer.
   */
  TObject* dereference(TinyPtrT tinyptr, std::pair<uint64_t, uint64_t> h);

  /**
   * "Dereferences" a given tiny pointer and returns a pointer to the object that it points to.
   * @param tinyptr The tiny pointer.
   * @param h A pair of hash values from the first and second hash function associated with the tiny pointer.
   * @return A pointer to the object pointed at by the tiny pointer.
   */
  const TObject* dereference(TinyPtrT tinyptr,
                             std::pair<uint64_t, uint64_t> h) const;

private:
  TObject* get_data_object(size_t data_table_index, size_t object_index);

  const TObject* get_data_object(size_t data_table_index,
                                 size_t object_index) const;
};

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
DerefTable<TObject, TTinyPtr, STinyPtr>::DerefTable(
    const uint32_t ht_bucket_count)
  : _ht_prime{primes::Prime::pick(ht_bucket_count)},
    _size{0},
    _num_buckets(_ht_prime.get()),
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
DerefTable<TObject, TTinyPtr, STinyPtr> DerefTable<
  TObject, TTinyPtr, STinyPtr>::Create(
    const size_t max_capacity) {
  const auto min_bucket_count =
      static_cast<uint32_t>((max_capacity + ENTRIES_PER_BIN_COUNT - 1) /
                            ENTRIES_PER_BIN_COUNT);
  // divide the minimum bucket count by the expected load factor estimate
  return DerefTable(
      static_cast<uint32_t>(static_cast<double>(min_bucket_count) / load_factor(
                                ENTRIES_PER_BIN_COUNT)));
}

inline void throw_fill_factor_exception(uint32_t size, uint32_t capacity) {
  const auto fill_factor = static_cast<float>(size) / static_cast<float>(
                             capacity);
  throw std::runtime_error(
      "Unable to allocate new object at fill factor " + std::to_string(
          fill_factor) +
      ". Size: " + std::to_string(size) + ", Capacity: " + std::to_string(
          capacity));
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
std::pair<typename DerefTable<TObject, TTinyPtr, STinyPtr>::TinyPtrT, TObject*>
DerefTable<TObject, TTinyPtr, STinyPtr>::allocate(
    const std::pair<uint64_t, uint64_t> h, const TTinyPtr special) {
  if (_size.load(std::memory_order::relaxed) >= _capacity) {
    throw_fill_factor_exception(_size.load(std::memory_order::relaxed),
                                _capacity);
  }

  auto h1_index = _ht_prime.mod(h.first);
  auto h2_index = _ht_prime.mod(h.second);

  auto& h1_meta_data = meta_table[h1_index];
  auto& h2_meta_data = meta_table[h2_index];

  ExclusiveLock<uint8_t> h_excl_lock;

retry: {
    auto h1_opt_lock = h1_meta_data.lock.lock_optimistically();
    auto h2_opt_lock = h2_meta_data.lock.lock_optimistically();

    const TTinyPtr h1_free_count = h1_meta_data.free_slot_count.load(
        std::memory_order::relaxed);
    const TTinyPtr h2_free_count = h2_meta_data.free_slot_count.load(
        std::memory_order::relaxed);

    uint8_t h_bit = h1_free_count >= h2_free_count ? 0 : 1;
    auto h_index = h_bit ? h2_index : h1_index;
    auto& h_meta_data = h_bit ? h2_meta_data : h1_meta_data;
    auto& h_meta_data_lock = h_bit ? h2_opt_lock : h1_opt_lock;
    auto& other_meta_data_lock = h_bit ? h1_opt_lock : h2_opt_lock;

    const TTinyPtr object_index = h_meta_data.free_slot_index.load(
        std::memory_order::relaxed);
    auto& object_entry = *get_data_object(h_index, object_index);
    const TTinyPtr next_free_slot_index = data_table[h_index].
        get_next_free_slot_index(object_index);

    // Note: for the case where h1 = h2 we have to make sure we validate the optimistic lock before upgrading
    if (!other_meta_data_lock.validate() || !h_meta_data_lock.
        try_upgrade_to_exclusive(h_excl_lock)) { goto retry; }

    if (h_meta_data.free_slot_count == 0) {
      throw_fill_factor_exception(_size.load(std::memory_order::relaxed),
                                  _capacity);
    }
    assert(
        object_index < ENTRIES_PER_BIN_COUNT &&
        "used free_slot_index out of bounds");

    h_meta_data.free_slot_count.fetch_sub(1, std::memory_order::relaxed);
    h_meta_data.free_slot_index.store(next_free_slot_index,
                                      std::memory_order::relaxed);

    h_excl_lock.unlock();

    _size.fetch_add(1, std::memory_order::relaxed);

    if constexpr (ZERO_NEW_ALLOCATED_MEMORY) {
      memset(&object_entry, 0, sizeof(TObject));
    }

    return {TinyPtrT(object_index, special, h_bit), &object_entry};
  }
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
void DerefTable<TObject, TTinyPtr, STinyPtr>::free(
    const TinyPtrT tinyptr, const std::pair<uint64_t, uint64_t> h) {
  const uint64_t hash = tinyptr.hash_fn() ? h.second : h.first;
  TTinyPtr index = tinyptr.index();
  const auto h_index = _ht_prime.mod(hash);

  auto& h_meta_data = meta_table[h_index];

  auto excl_lock = h_meta_data.lock.lock_exclusive();

  data_table[h_index].set_next_free_slot_index(index,
                                               h_meta_data.free_slot_index.load(
                                                   std::memory_order::relaxed));
  h_meta_data.free_slot_index.store(index, std::memory_order::relaxed);
  h_meta_data.free_slot_count.fetch_add(1, std::memory_order::relaxed);

  excl_lock.unlock();

  _size.fetch_sub(1, std::memory_order::relaxed);
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
TObject* DerefTable<TObject, TTinyPtr, STinyPtr>::dereference(
    const TinyPtrT tinyptr, const std::pair<uint64_t, uint64_t> h) {
  return const_cast<TObject*>(static_cast<const DerefTable*>(this)->dereference(
      tinyptr, h));
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
const TObject* DerefTable<TObject, TTinyPtr, STinyPtr>::dereference(
    const TinyPtrT tinyptr, const std::pair<uint64_t, uint64_t> h) const {
  if (tinyptr == TinyPtrT::null) { return nullptr; }
  if (tinyptr == TinyPtrT::tagged) {
    throw std::runtime_error("Cannot dereference tagged tinyptr");
  }

  const uint64_t hash = tinyptr.hash_fn() ? h.second : h.first;
  const TTinyPtr index = tinyptr.index();

  return get_data_object(_ht_prime.mod(hash), index);
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
TObject* DerefTable<TObject, TTinyPtr, STinyPtr>::get_data_object(
    const size_t data_table_index,
    const size_t object_index) {
  return const_cast<TObject*>(static_cast<const DerefTable*>(this)->
    get_data_object(data_table_index, object_index));
}

template <typename TObject, std::unsigned_integral TTinyPtr, unsigned STinyPtr>
const TObject* DerefTable<TObject, TTinyPtr, STinyPtr>::get_data_object(
    size_t data_table_index,
    size_t object_index) const {
  // convert TinyPtr to object pointer without any memory lookups!
  return reinterpret_cast<const TObject*>(&data_table[data_table_index].entries[object_index]);
}