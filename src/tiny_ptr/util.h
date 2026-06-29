#pragma once

#include <cstdint>
#include <utility>

inline uint64_t djb2_hash_init = 5381;

inline uint64_t sdbm_hash_init = 0;

inline uint64_t djb2_hash(const char c, const uint64_t h) {
  return (h << 5) + h + c;
}

inline uint64_t sdbm_hash(const char c, const uint64_t h) {
  return (h << 6) + (h << 16) - h + c;
}

inline std::pair<uint64_t, uint64_t> djb2_sdbm_hash(
    const char c, const std::pair<uint64_t, uint64_t>& h) {
  return {djb2_hash(c, h.first), sdbm_hash(c, h.second)};
}

inline std::pair<uint64_t, uint64_t> address_hash(
    const void* ptr, const uint8_t shift) {
  const auto h = reinterpret_cast<uint64_t>(ptr);
  return {h + shift, h - shift};
}