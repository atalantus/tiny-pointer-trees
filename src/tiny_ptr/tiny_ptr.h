#pragma once

#include <cassert>
#include <concepts>
#include <cstdint>

/**
 * @class TinyPtr
 * @brief A tiny pointer.
 *
 * The TinyPtr value consists of the lower 1-bit indicating which hash function is used,
 * and the upper S-bits are used for storing special information inside the tiny pointer
 * and the remaining bits representing the index starting at one such that the first element has index 1.
 *
 * By starting the indexing at one, we have two special values:
 *  - Hash-Bit 0, Index 0: We use this to indicate a null pointer
 *  - Hash-Bit 1, Index 0: We use this to indicate a special/tagged pointer
 */
template <std::unsigned_integral T = uint8_t, unsigned S = 0>
struct TinyPtr {
  static_assert((sizeof(T) * 8) > S + 1 && "there must be at least one bit left for the index");

  using value_type             = T;
  static constexpr unsigned SB = S;

  T value;

  constexpr TinyPtr() : value(0) {}

  explicit constexpr TinyPtr(const T value) : value(value) {}

  constexpr TinyPtr(const T index, const bool h)
      : value(((index + 1) << (1 + S)) | h) /* plus 1 preserve null and tagged tinyptr */ {
    assert(index < (1 << (sizeof(T) * 8 - S - 1)) - 1);
  }

  constexpr TinyPtr(const T index, const T special, const bool h)
      : value(((index + 1) << (1 + S)) | (special << 1) | h) /* plus 1 preserve null and tagged tinyptr */ {
    assert(index < (1 << (sizeof(T) * 8 - S - 1)) - 1);
    assert(special < (1 << S));
  }

  static const TinyPtr null;
  static const TinyPtr tagged;

  operator bool() const { return value != 0; }

  bool operator==(const TinyPtr &other) const { return value == other.value; }

  bool operator!=(const TinyPtr &other) const { return value != other.value; }

  [[nodiscard]] bool hash_fn() const {
    assert(*this != TinyPtr::null && *this != TinyPtr::tagged && "hash_fn() called on null/tagged tinyptr");
    return value & 1;
  }

  [[nodiscard]] T index() const {
    assert(*this != TinyPtr::null && *this != TinyPtr::tagged && "index() called on null/tagged tinyptr");
    return (value >> (1 + S)) - 1; /* minus 1 to preserve null and tagged tinyptr */
  }

  [[nodiscard]] T special() const {
    assert(*this != TinyPtr::null && *this != TinyPtr::tagged && "special() called on null/tagged tinyptr");
    return (value >> 1) & ((1 << S) - 1);
  }
};

static_assert(sizeof(TinyPtr<>) == 1);

template <std::unsigned_integral T, unsigned S>
inline const TinyPtr<T, S> TinyPtr<T, S>::null{0};

template <std::unsigned_integral T, unsigned S>
inline const TinyPtr<T, S> TinyPtr<T, S>::tagged{1};
