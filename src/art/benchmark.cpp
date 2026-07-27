#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "benchmark.hpp"
#include "ARTSynchronized/OptimisticLockCoupling/Tree.h"
#include "tbb/tbb.h"
#include "tiny_art/Tree.h"
#include "tiny_art_256/Tree.h"
#include "tiny_art_64/Tree.h"

struct Timings {
  uint64_t insertMicroseconds = 0;
  uint64_t lookupMicroseconds = 0;
};

template <typename TOperation>
static uint64_t runOperation(tbb::task_arena& arena, uint64_t count,
                             TOperation operation) {
  const auto start = std::chrono::steady_clock::now();
  arena.execute([&] {
    tbb::parallel_for(tbb::blocked_range<uint64_t>(0, count),
                      [&](const tbb::blocked_range<uint64_t>& range) {
                        operation(range.begin(), range.end());
                      });
  });
  return std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start)
      .count();
}

template <typename TArt, typename TInsertCallback>
static Timings runBenchmarkIteration(TArt tree,
                                     const std::vector<uint64_t>& keys,
                                     std::size_t threadCount,
                                     TInsertCallback insertCallback) {
  keyValues = &keys;
  const auto count = keys.size();
  tbb::task_arena arena(threadCount);

  const auto insert = [&](uint64_t begin, uint64_t end) {
    auto threadInfo = tree.getThreadInfo();
    for (uint64_t i = begin; i != end; ++i) {
      Key key;
      loadKey(i + 1, key);
      tree.insert(key, i + 1, threadInfo);
    }
  };

  const auto lookup = [&](uint64_t begin, uint64_t end) {
    auto threadInfo = tree.getThreadInfo();
    for (uint64_t i = begin; i != end; ++i) {
      Key key;
      const auto expected = i + 1;
      loadKey(expected, key);
#if DEBUG
      if (tree.lookup(key, threadInfo) != expected) {
        std::cerr << "wrong key read" << std::endl;
        std::abort();
      }
#else
      tree.lookup(key, threadInfo);
#endif
    }
  };

  auto insertMicroseconds = runOperation(arena, count, insert);
  insertCallback(tree);

  auto lookupMicroseconds = runOperation(arena, count, lookup);

  return {.insertMicroseconds = insertMicroseconds,
          .lookupMicroseconds = lookupMicroseconds};
}

static void addTimings(Timings& total, const Timings& timings) {
  total.insertMicroseconds += timings.insertMicroseconds;
  total.lookupMicroseconds += timings.lookupMicroseconds;
}

int main() {
  std::vector<BenchmarkRow> results;

  for (const auto threadCount : threadCounts) {
    Timings olcTotal;
    Timings tinyOlcTotal;
    Timings tiny64OlcTotal;
    Timings tiny256OlcTotal;

    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
      const auto keys = generateKeys(keyCount);

      std::cout << "Iteration " << iteration + 1 << '/' << iterations << ", "
          << threadCount << " threads: art_olc" << std::endl;
      addTimings(olcTotal,
                 runBenchmarkIteration(ART_OLC::Tree(loadKey), keys,
                                       threadCount, [](ART_OLC::Tree&) {
                                       }));

      std::cout << "Iteration " << iteration + 1 << '/' << iterations << ", "
          << threadCount << " threads: tiny_art_olc" << std::endl;
      addTimings(tinyOlcTotal,
                 runBenchmarkIteration(
                     TINY_ART_OLC::Tree(loadKey, tinyOlcNodeCounts[0],
                                        tinyOlcNodeCounts[1],
                                        tinyOlcNodeCounts[2],
                                        tinyOlcNodeCounts[3]),
                     keys,
                     threadCount,
                     [](TINY_ART_OLC::Tree& tree) {
                       tree.deref_tables.
                           printDerefTableSizes();
                     }));

      std::cout << "Iteration " << iteration + 1 << '/' << iterations << ", "
          << threadCount << " threads: tiny_art_64_olc" << std::endl;
      addTimings(tiny64OlcTotal,
                 runBenchmarkIteration(
                     TINY_ART_64_OLC::Tree(loadKey, tiny64OlcNodeCounts[0],
                                           tiny64OlcNodeCounts[1],
                                           tiny64OlcNodeCounts[2],
                                           tiny64OlcNodeCounts[3]),
                     keys,
                     threadCount,
                     [](TINY_ART_64_OLC::Tree& tree) {
                       tree.deref_tables.
                           printDerefTableSizes();
                     }));

      std::cout << "Iteration " << iteration + 1 << '/' << iterations << ", "
          << threadCount << " threads: tiny_art_256_olc" << std::endl;
      addTimings(tiny256OlcTotal,
                 runBenchmarkIteration(
                     TINY_ART_256_OLC::Tree(loadKey, tiny256OlcNodeCounts[0],
                                            tiny256OlcNodeCounts[1]),
                     keys,
                     threadCount,
                     [](TINY_ART_256_OLC::Tree& tree) {
                       tree.deref_tables.
                           printDerefTableSizes();
                     }));
    }

    results.push_back(
    {.treeName = "art_olc", .threadCount = threadCount,
     .millionInsertOperationsPerSecond = millionOperationsPerSecond(keyCount,
       olcTotal.insertMicroseconds / iterations),
     .millionLookupOperationsPerSecond = millionOperationsPerSecond(keyCount,
       olcTotal.lookupMicroseconds / iterations)});

    results.push_back(
    {.treeName = "tiny_art_olc", .threadCount = threadCount,
     .millionInsertOperationsPerSecond = millionOperationsPerSecond(
         keyCount, tinyOlcTotal.insertMicroseconds / iterations),
     .millionLookupOperationsPerSecond = millionOperationsPerSecond(
         keyCount, tinyOlcTotal.lookupMicroseconds / iterations)});

    results.push_back(
    {.treeName = "tiny_art_64_olc", .threadCount = threadCount,
     .millionInsertOperationsPerSecond = millionOperationsPerSecond(
         keyCount, tiny64OlcTotal.insertMicroseconds / iterations),
     .millionLookupOperationsPerSecond = millionOperationsPerSecond(
         keyCount, tiny64OlcTotal.lookupMicroseconds / iterations)});

    results.push_back(
    {.treeName = "tiny_art_256_olc", .threadCount = threadCount,
     .millionInsertOperationsPerSecond = millionOperationsPerSecond(
         keyCount, tiny256OlcTotal.insertMicroseconds / iterations),
     .millionLookupOperationsPerSecond = millionOperationsPerSecond(
         keyCount, tiny256OlcTotal.lookupMicroseconds / iterations)});
  }

  printThroughputTable(results);
}