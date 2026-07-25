#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

#include "tbb/tbb.h"
#include "Key.h"
#include "ARTSynchronized/OptimisticLockCoupling/Tree.h"
#include "tiny_art/Tree.h"
#include "tiny_art_256/Tree.h"
#include "tiny_art_64/Tree.h"

struct Timings {
  uint64_t insertMicroseconds = 0;
  uint64_t lookupMicroseconds = 0;
};

struct BenchmarkRow {
  const char* treeName;
  std::size_t threadCount;
  double millionInsertOperationsPerSecond;
  double millionLookupOperationsPerSecond;
};

const std::vector<uint64_t>* keyValues;

void loadKey(TID tid, Key& key) {
  const auto keyValue = (*keyValues)[(tid - 1)];
  key.setKeyLen(sizeof(keyValue));
  reinterpret_cast<uint64_t*>(&key[0])[0] = __builtin_bswap64(keyValue);
}

uint64_t splitmix64(uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

std::vector<uint64_t> generateKeys(std::size_t count, std::size_t distribution) {
  std::vector<uint64_t> keys(count);

  if (distribution == 3) {
    for (std::size_t i = 0; i < count; ++i) {
      keys[i] = splitmix64(i);
    }
    return keys;
  }

  for (std::size_t i = 0; i < count; ++i) {
    keys[i] = i + 1;
  }
  if (distribution == 1) {
    std::mt19937_64 generator(0);
    std::shuffle(keys.begin(), keys.end(), generator);
  } else if (distribution == 2) {
    constexpr uint64_t pseudoSparseMask =
        std::numeric_limits<uint64_t>::max() >> 2;
    constexpr uint64_t multiplier = 0x1f123bb5ULL;
    constexpr uint64_t offset = 0x1a2b3c4d5e6fULL;
    for (std::size_t i = 0; i < count; ++i) {
      keys[i] = (static_cast<uint64_t>(i) * multiplier + offset) &
                pseudoSparseMask;
    }
  }

  return keys;
}

template <typename TArt>
Timings multithreaded(const std::vector<uint64_t>& keys,
                     std::size_t threadCount) {
  keyValues = &keys;
  const auto count = keys.size();
  TArt tree(loadKey, keys.size());
  tbb::task_arena arena(threadCount);

  const auto insertStart = std::chrono::steady_clock::now();
  arena.execute([&] {
    tbb::parallel_for(
        tbb::blocked_range<uint64_t>(0, count),
        [&](const tbb::blocked_range<uint64_t>& range) {
          auto threadInfo = tree.getThreadInfo();
          for (uint64_t i = range.begin(); i != range.end(); ++i) {
            Key key;
            const auto tid = i + 1;
            loadKey(tid, key);
            tree.insert(key, tid, threadInfo);
          }
        });
  });
  const auto insertDuration = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - insertStart);

  const auto lookupStart = std::chrono::steady_clock::now();
  arena.execute([&] {
    tbb::parallel_for(
        tbb::blocked_range<uint64_t>(0, count),
        [&](const tbb::blocked_range<uint64_t>& range) {
          auto threadInfo = tree.getThreadInfo();
          for (uint64_t i = range.begin(); i != range.end(); ++i) {
            Key key;
            const auto expected = i + 1;
            loadKey(expected, key);
            const auto value = tree.lookup(key, threadInfo);
            if (value != expected) {
              std::cerr << "wrong key read: " << value
                        << " expected: " << expected << std::endl;
              std::abort();
            }
          }
        });
  });
  const auto lookupDuration = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - lookupStart);

  return {static_cast<uint64_t>(insertDuration.count()),
          static_cast<uint64_t>(lookupDuration.count())};
}

void addTimings(Timings& total, const Timings& timings) {
  total.insertMicroseconds += timings.insertMicroseconds;
  total.lookupMicroseconds += timings.lookupMicroseconds;
}

double operationsPerSecond(std::size_t count, uint64_t microseconds) {
  return microseconds == 0 ? 0.0 : (count * 1000000.0) / microseconds;
}

BenchmarkRow averageRow(const char* treeName, std::size_t threadCount,
                        std::size_t keyCount, std::size_t iterations,
                        const Timings& total) {
  const auto insertAverage = total.insertMicroseconds / iterations;
  const auto lookupAverage = total.lookupMicroseconds / iterations;
  return {treeName, threadCount,
          operationsPerSecond(keyCount, insertAverage) / 1000000.0,
          operationsPerSecond(keyCount, lookupAverage) / 1000000.0};
}

void logProgress(std::size_t iteration, std::size_t iterations,
                 std::size_t threadCount, const char* treeName) {
  std::cout << "Iteration " << iteration << '/' << iterations << ", "
            << threadCount << " threads: " << treeName << std::endl;
}

void printTable(const std::vector<BenchmarkRow>& rows) {
  constexpr int treeWidth = 22;
  constexpr int threadWidth = 10;
  constexpr int operationWidth = 24;

  std::cout << '\n' << std::left << std::setw(treeWidth) << "tree"
            << std::right << std::setw(threadWidth) << "threads"
            << std::setw(operationWidth) << "million insert_ops/s"
            << std::setw(operationWidth) << "million lookup_ops/s" << '\n';
  for (const auto& row : rows) {
    std::cout << std::left << std::setw(treeWidth) << row.treeName
              << std::right << std::setw(threadWidth) << row.threadCount
              << std::fixed << std::setprecision(2)
              << std::setw(operationWidth)
              << row.millionInsertOperationsPerSecond
              << std::setw(operationWidth)
              << row.millionLookupOperationsPerSecond << '\n';
  }
}

bool parsePositiveSize(const char* text, std::size_t& value) {
  if (*text == '\0' || *text == '-') {
    return false;
  }

  errno = 0;
  char* end = nullptr;
  const auto parsed = std::strtoull(text, &end, 10);
  if (errno == ERANGE || *end != '\0' || parsed == 0 ||
      parsed > std::numeric_limits<std::size_t>::max()) {
    return false;
  }

  value = static_cast<std::size_t>(parsed);
  return true;
}

int main() {
  std::size_t keyCount = 5'000'000;
  std::size_t iterations = 3;
  constexpr std::size_t distribution = 3;
  std::vector<size_t> threadCounts = {/*1, 2,*/ 4/*, 8, 16, 24*/};

  const auto keys = generateKeys(keyCount, distribution);
  std::vector<BenchmarkRow> results;
  results.reserve(threadCounts.size() * 4);

  for (const auto threadCount : threadCounts) {
    Timings olcTotal;
    Timings tinyOlcTotal;
    Timings tiny64OlcTotal;
    Timings tiny256OlcTotal;

    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
      logProgress(iteration + 1, iterations, threadCount, "art_olc");
      addTimings(olcTotal, multithreaded<ART_OLC::Tree>(keys, threadCount));
      logProgress(iteration + 1, iterations, threadCount, "tiny_art_olc");
      addTimings(tinyOlcTotal,
                 multithreaded<TINY_ART_OLC::Tree>(keys, threadCount));
      logProgress(iteration + 1, iterations, threadCount, "tiny_art_64_olc");
      addTimings(tiny64OlcTotal,
                 multithreaded<TINY_ART_64_OLC::Tree>(keys, threadCount));
      logProgress(iteration + 1, iterations, threadCount,
                  "tiny_art_256_olc");
      addTimings(tiny256OlcTotal,
                 multithreaded<TINY_ART_256_OLC::Tree>(keys, threadCount));
    }

    results.push_back(
        averageRow("art_olc", threadCount, keyCount, iterations, olcTotal));
    results.push_back(averageRow("tiny_art_olc", threadCount, keyCount,
                                 iterations, tinyOlcTotal));
    results.push_back(averageRow("tiny_art_64_olc", threadCount, keyCount,
                                 iterations, tiny64OlcTotal));
    results.push_back(averageRow("tiny_art_256_olc", threadCount, keyCount,
                                 iterations, tiny256OlcTotal));
  }

  printTable(results);

  return 0;
}
