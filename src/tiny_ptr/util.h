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

inline uint64_t mix64(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

inline std::pair<uint64_t, uint64_t> id_hash(
    const uint64_t id, const uint8_t shift) {
  const uint64_t combined = id * 0x9E3779B97F4A7C15ULL + shift;
  return {mix64(combined), mix64(combined ^ 0xD6E8FEB86659FD93ULL)};
}

inline TinyPtrHashes word_hash(const std::vector<char>& word) {
  // TODO: Leave as placeholder for now
  return {0, 0};
}