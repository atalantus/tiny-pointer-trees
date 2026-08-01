#pragma once

#include <bit>
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

// Odd, mutually unrelated multipliers. Oddness makes
// shift -> low byte of (shift * K) a bijection on 0..255.
inline constexpr uint64_t id_hash_k1 = 0x2545F4914F6CDD1DULL;
inline constexpr uint64_t id_hash_k2 = 0x9E3779B97F4A7C15ULL;

/**
 * Derives the two tiny pointer hash values from the id of the node owning the
 * pointer and the key byte its child hangs under.
 *
 * PRECONDITION: id is already uniformly distributed over the 64-bit range.
 * next_node_id() (art/tiny_art_common.hpp) guarantees this by running a
 * per-thread counter through SplitMix64, so avalanching it again here would be
 * pure latency. If id ever becomes structured -- a plain counter, a pointer, an
 * externally supplied key -- this breaks, and one avalanche pass has to come
 * back; tools/hash_distribution_sim.cpp carries that fallback as its
 * "proposed+mix64" variant, already measured.
 *
 * Both multiplies depend only on shift, a key byte the caller already holds, so
 * they issue while the load of the owning node's id is still in flight. Only the
 * final xor sits on the id-dependent critical path, which matters because that
 * path runs between one node's cache miss and the address of the next.
 *
 * Deriving the second value by rotation rather than a second mixing pass makes
 * the two hashes read disjoint 32-bit fields of id under a low-bit-mask
 * reduction (USE_PRIMES=0), so the two bucket indices are then independent by
 * construction for tables up to 2^32 buckets -- which is what the
 * power-of-two-choices placement in DerefTable::allocate_impl assumes. Under
 * the default Prime::mod reduction both indices depend on all 64 bits and that
 * independence is empirical instead; hash_distribution_sim measures either way.
 */
inline std::pair<uint64_t, uint64_t> id_hash(
    const uint64_t id, const uint8_t shift) {
  return {id ^ (static_cast<uint64_t>(shift) * id_hash_k1),
          std::rotl(id, 32) ^ (static_cast<uint64_t>(shift) * id_hash_k2)};
}

inline TinyPtrHashes word_hash(const std::vector<char>& word) {
  // TODO: Leave as placeholder for now
  return {0, 0};
}