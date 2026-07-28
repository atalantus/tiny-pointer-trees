#pragma once

#include <random>
#include <cstdint>

/**
 * Generates a well-distributed 64-bit id for a node without shared state.
 *
 * Each thread has an independently seeded counter, so generating an id does
 * not require synchronization. SplitMix64 guarantees no repeats within a
 * thread until the counter wraps; ids from different threads may collide with
 * negligible probability.
 */
inline uint64_t next_node_id() {
  thread_local uint64_t counter = [] {
    std::random_device random;
    return (static_cast<uint64_t>(random()) << 32) ^
           static_cast<uint64_t>(random());
  }();

  uint64_t z = counter += 0x9E3779B97F4A7C15ULL;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}