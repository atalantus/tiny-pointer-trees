#pragma once

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

#include "Key.h"

inline std::size_t keyCount = 5'000'000;
inline std::size_t iterations = 3;
inline constexpr std::size_t distribution = 3;
inline std::vector<std::size_t> threadCounts = {
    /*1, 2,*/ 4 /*, 8, 16, 24*/};
inline std::size_t seed = 0;
inline const std::vector<uint64_t>* keyValues;

inline std::array<size_t, 4> tinyOlcNodeCounts = {
    600'000, 60'000, 65'000, keyCount};
inline std::array<size_t, 4> tiny64OlcNodeCounts = {
    600'000, 60'000, 38'000, keyCount};
inline std::array<size_t, 2> tiny256OlcNodeCounts = {668'000, keyCount};

struct BenchmarkRow {
  const char* treeName;
  std::size_t threadCount;
  double millionInsertOperationsPerSecond;
  double millionLookupOperationsPerSecond;
};

inline void loadKey(uint64_t tid, Key& key) {
  const auto keyValue = (*keyValues)[tid - 1];
  key.setKeyLen(sizeof(keyValue));
  reinterpret_cast<uint64_t*>(&key[0])[0] = __builtin_bswap64(keyValue);
}

inline uint64_t splitmix64(uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

inline std::vector<uint64_t> generateKeys(std::size_t count) {
  std::vector<uint64_t> keys(count);

  // distribution 3: actual random 8-byte integers
  if (distribution == 3) {
    for (std::size_t i = 0; i < count; ++i) {
      keys[i] = splitmix64(i + ++seed);
    }
    return keys;
  }

  // distribution 0: sorted, dense integers
  for (std::size_t i = 0; i < count; ++i) {
    keys[i] = i + 1;
  }

  // distribution 1: sorted, dense integers in random order
  if (distribution == 1) {
    std::shuffle(keys.begin(), keys.end(), std::mt19937_64(++seed));
  } else if (distribution == 2) {
    // distribution 2: "pseudo-sparse" (the most-significant leaf bit gets lost)
    constexpr auto mask = std::numeric_limits<uint64_t>::max() >> 2;
    constexpr uint64_t multiplier = 0x1f123bb5ULL;
    constexpr uint64_t offset = 0x1a2b3c4d5e6fULL;
    for (std::size_t i = 0; i < count; ++i) {
      keys[i] = (i * multiplier + offset) & mask;
    }
  }

  return keys;
}

inline double millionOperationsPerSecond(std::size_t count,
                                         uint64_t microseconds) {
  return microseconds == 0 ? 0.0 : count / static_cast<double>(microseconds);
}

inline void printThroughputTable(const std::vector<BenchmarkRow>& rows) {
  constexpr int treeWidth = 22;
  constexpr int threadWidth = 10;
  constexpr int operationWidth = 24;
  std::cout << '\n'
      << std::left << std::setw(treeWidth) << "tree" << std::right
      << std::setw(threadWidth) << "threads" << std::setw(operationWidth)
      << "million insert_ops/s" << std::setw(operationWidth)
      << "million lookup_ops/s" << '\n';
  for (const auto& row : rows) {
    std::cout << std::left << std::setw(treeWidth) << row.treeName << std::right
        << std::setw(threadWidth) << row.threadCount << std::fixed
        << std::setprecision(2) << std::setw(operationWidth)
        << row.millionInsertOperationsPerSecond
        << std::setw(operationWidth)
        << row.millionLookupOperationsPerSecond << '\n';
  }
}