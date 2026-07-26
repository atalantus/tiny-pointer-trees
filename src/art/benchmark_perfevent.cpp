#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

#include "tbb/tbb.h"
#include "benchmark.hpp"
#include "ARTSynchronized/OptimisticLockCoupling/Tree.h"
#include "tiny_art/Tree.h"
#include "util/PerfEvent.hpp"

struct PerfCounters {
  double cycles = 0;
  double kernelCycles = 0;
  double instructions = 0;
  double l1Misses = 0;
  double cacheMisses = 0;
  double branchMisses = 0;
  double taskNanoseconds = 0;
  bool available = false;
};

struct Timings {
  uint64_t insertMicroseconds = 0;
  uint64_t lookupMicroseconds = 0;
  PerfCounters insertCounters;
  PerfCounters lookupCounters;
  std::size_t insertCounterSamples = 0;
  std::size_t lookupCounterSamples = 0;
};

struct PerfBenchmarkRow {
  BenchmarkRow throughput;
  PerfCounters insertCounters;
  PerfCounters lookupCounters;
};

PerfCounters readCounters(PerfEvent& event) {
  if (event.events.empty()) {
    return {};
  }
  return {event.getCounter("cycle"),  event.getCounter("kcycle"),
          event.getCounter("instr"),  event.getCounter("L1-miss"),
          event.getCounter("c-miss"), event.getCounter("br-miss"),
          event.getCounter("task"),   true};
}

void addCounters(PerfCounters& total, const PerfCounters& counters) {
  total.cycles += counters.cycles;
  total.kernelCycles += counters.kernelCycles;
  total.instructions += counters.instructions;
  total.l1Misses += counters.l1Misses;
  total.cacheMisses += counters.cacheMisses;
  total.branchMisses += counters.branchMisses;
  total.taskNanoseconds += counters.taskNanoseconds;
  total.available = total.available || counters.available;
}

template <typename TOperation>
uint64_t runOperation(tbb::task_arena& arena, uint64_t count,
                  std::size_t threadCount, TOperation operation,
                  PerfCounters& counters) {
  const auto workerCount = std::min<std::size_t>(threadCount, count);
  std::vector<PerfCounters> workerCounters(workerCount);

  const auto start = std::chrono::steady_clock::now();

  arena.execute([&] {
    tbb::parallel_for(std::size_t{0}, workerCount, [&](std::size_t worker) {
      const auto begin = count * worker / workerCount;
      const auto end = count * (worker + 1) / workerCount;
      PerfEvent event;
      event.startCounters();
      operation(begin, end);
      event.stopCounters();
      workerCounters[worker] = readCounters(event);
    });
  });

  const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);

  counters = {};
  for (const auto& workerCounter : workerCounters) {
    if (!workerCounter.available) {
      return duration.count();
    }
    addCounters(counters, workerCounter);
  }
  return duration.count();
}

template <typename TArt>
Timings multithreaded(TArt tree, const std::vector<uint64_t>& keys,
                      std::size_t threadCount) {
  keyValues = &keys;
  const auto count = keys.size();
  tbb::task_arena arena(threadCount);
  const auto insert = [&](uint64_t begin, uint64_t end) {
    auto threadInfo = tree.getThreadInfo();
    for (uint64_t i = begin; i != end; ++i) {
      Key key;
      const auto tid = i + 1;
      loadKey(tid, key);
      tree.insert(key, tid, threadInfo);
    }
  };
  const auto lookup = [&](uint64_t begin, uint64_t end) {
    auto threadInfo = tree.getThreadInfo();
    for (uint64_t i = begin; i != end; ++i) {
      Key key;
      const auto expected = i + 1;
      loadKey(expected, key);
      if (tree.lookup(key, threadInfo) != expected) {
        std::cerr << "wrong key read" << std::endl;
        std::abort();
      }
    }
  };

  PerfCounters insertCounters;
  PerfCounters lookupCounters;
  return {runOperation(arena, count, threadCount, insert, insertCounters),
          runOperation(arena, count, threadCount, lookup, lookupCounters),
          insertCounters,
          lookupCounters,
          insertCounters.available ? 1U : 0U,
          lookupCounters.available ? 1U : 0U};
}

void addTimings(Timings& total, const Timings& timings) {
  total.insertMicroseconds += timings.insertMicroseconds;
  total.lookupMicroseconds += timings.lookupMicroseconds;
  if (timings.insertCounters.available) {
    addCounters(total.insertCounters, timings.insertCounters);
    total.insertCounterSamples += timings.insertCounterSamples;
  }
  if (timings.lookupCounters.available) {
    addCounters(total.lookupCounters, timings.lookupCounters);
    total.lookupCounterSamples += timings.lookupCounterSamples;
  }
}

PerfCounters averageCounters(PerfCounters counters, std::size_t samples) {
  if (samples == 0) {
    return {};
  }
  const auto scale = static_cast<double>(keyCount) * samples;
  counters.cycles /= scale;
  counters.kernelCycles /= scale;
  counters.instructions /= scale;
  counters.l1Misses /= scale;
  counters.cacheMisses /= scale;
  counters.branchMisses /= scale;
  counters.taskNanoseconds /= scale;
  return counters;
}

PerfBenchmarkRow averageRow(const char* treeName, std::size_t threadCount,
                            const Timings& total) {
  return {{treeName,
           threadCount,
           millionOperationsPerSecond(keyCount,
                                      total.insertMicroseconds / iterations),
           millionOperationsPerSecond(keyCount,
                                      total.lookupMicroseconds / iterations)},
          averageCounters(total.insertCounters, total.insertCounterSamples),
          averageCounters(total.lookupCounters, total.lookupCounterSamples)};
}

void printTable(const std::vector<PerfBenchmarkRow>& rows) {
  std::vector<benchmark::BenchmarkRow> throughput;
  throughput.reserve(rows.size());
  for (const auto& row : rows) {
    throughput.push_back(row.throughput);
  }
  printThroughputTable(throughput);

  constexpr int treeWidth = 22;
  constexpr int threadWidth = 10;
  constexpr int operationWidth = 24;
  constexpr int phaseWidth = 10;
  constexpr int counterWidth = 18;
  std::cout << '\n'
            << "PerfEvent counters (average per operation across iterations)\n"
            << std::left << std::setw(treeWidth) << "tree" << std::right
            << std::setw(threadWidth) << "threads" << std::setw(phaseWidth)
            << "operation" << std::setw(counterWidth) << "cycles/op"
            << std::setw(counterWidth) << "kcycles/op"
            << std::setw(counterWidth) << "instructions/op"
            << std::setw(counterWidth) << "L1-misses/op"
            << std::setw(counterWidth) << "cache-misses/op"
            << std::setw(counterWidth) << "branch-misses/op"
            << std::setw(counterWidth) << "task-ns/op" << '\n';

  const auto printCounters = [&](const PerfBenchmarkRow& row,
                                 const char* phase,
                                 const PerfCounters& counters) {
    std::cout << std::left << std::setw(treeWidth) << row.throughput.treeName
              << std::right << std::setw(threadWidth)
              << row.throughput.threadCount
              << std::setw(phaseWidth) << phase;
    if (!counters.available) {
      std::cout << std::setw(counterWidth * 7) << "unavailable" << '\n';
      return;
    }
    std::cout << std::fixed << std::setprecision(2)
              << std::setw(counterWidth) << counters.cycles
              << std::setw(counterWidth) << counters.kernelCycles
              << std::setw(counterWidth) << counters.instructions
              << std::setw(counterWidth) << counters.l1Misses
              << std::setw(counterWidth) << counters.cacheMisses
              << std::setw(counterWidth) << counters.branchMisses
              << std::setw(counterWidth) << counters.taskNanoseconds << '\n';
  };
  for (const auto& row : rows) {
    printCounters(row, "insert", row.insertCounters);
    printCounters(row, "lookup", row.lookupCounters);
  }
}

int main() {
  std::vector<PerfBenchmarkRow> results;
  results.reserve(threadCounts.size() * 2);

  for (const auto threadCount : threadCounts) {
    Timings olcTotal;
    Timings tinyOlcTotal;
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
      const auto keys = generateKeys(keyCount);
      std::cout << "Iteration " << iteration + 1 << '/' << iterations << ", "
                << threadCount << " threads: art_olc" << std::endl;
      addTimings(olcTotal,
                 multithreaded(ART_OLC::Tree(loadKey), keys, threadCount));
      std::cout << "Iteration " << iteration + 1 << '/' << iterations << ", "
                << threadCount << " threads: tiny_art_olc" << std::endl;
      addTimings(tinyOlcTotal,
                 multithreaded(TINY_ART_OLC::Tree(loadKey, keyCount), keys,
                               threadCount));
    }
    results.push_back(averageRow("art_olc", threadCount, olcTotal));
    results.push_back(averageRow("tiny_art_olc", threadCount, tinyOlcTotal));
  }

  printTable(results);
}
