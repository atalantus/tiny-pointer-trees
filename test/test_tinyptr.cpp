#include "tiny_ptr/deref_table.h"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <mutex>
#include <random>
#include <ranges>
#include <thread>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"

using tnyptr_t = TinyPtr<>;

TEST(TestTinyPtr, TinyPtrParametrization) {
    using DerefTableU8 = DerefTable<uint32_t>;
    using DerefTableU8S1 = DerefTable<uint32_t, uint8_t, 1>;
    using DerefTableU8S2 = DerefTable<uint32_t, uint8_t, 2>;
    using DerefTableU16 = DerefTable<uint32_t, uint16_t>;
    using DerefTableU16S1 = DerefTable<uint32_t, uint16_t, 1>;
    using DerefTableU16S2 = DerefTable<uint32_t, uint16_t, 2>;
    using DerefTableU16S8 = DerefTable<uint32_t, uint16_t, 8>;

    ASSERT_EQ(DerefTableU8::ENTRIES_PER_BIN_COUNT, (1 << 7) - 1);
    ASSERT_EQ(DerefTableU8::TinyPtrT::null.value, 0);
    ASSERT_EQ(DerefTableU8::TinyPtrT::tagged.value, 1);
    ASSERT_EQ(DerefTableU8S1::ENTRIES_PER_BIN_COUNT, (1 << 6) - 1);
    ASSERT_EQ(DerefTableU8S1::TinyPtrT::null.value, 0);
    ASSERT_EQ(DerefTableU8S1::TinyPtrT::tagged.value, 1);
    ASSERT_EQ(DerefTableU8S2::ENTRIES_PER_BIN_COUNT, (1 << 5) - 1);
    ASSERT_EQ(DerefTableU16::ENTRIES_PER_BIN_COUNT, (1 << 15) - 1);
    ASSERT_EQ(DerefTableU16S1::ENTRIES_PER_BIN_COUNT, (1 << 14) - 1);
    ASSERT_EQ(DerefTableU16S2::ENTRIES_PER_BIN_COUNT, (1 << 13) - 1);
    ASSERT_EQ(DerefTableU16S8::ENTRIES_PER_BIN_COUNT, (1 << 7) - 1);
    ASSERT_EQ(DerefTableU16S8::TinyPtrT::null.value, 0);
    ASSERT_EQ(DerefTableU16S8::TinyPtrT::tagged.value, 1);

    auto t0 = DerefTableU16S8::TinyPtrT(0, 0, false);
    ASSERT_EQ(t0.hash_fn(), false);
    ASSERT_EQ(t0.special(), 0);
    ASSERT_EQ(t0.index(), 0);

    auto t1 = DerefTableU16S8::TinyPtrT(1, 1, true);
    ASSERT_EQ(t1.hash_fn(), true);
    ASSERT_EQ(t1.special(), 1);
    ASSERT_EQ(t1.index(), 1);

    auto t2 = DerefTableU16S8::TinyPtrT(DerefTableU16S8::ENTRIES_PER_BIN_COUNT - 1, 255, true);
    ASSERT_EQ(t2.hash_fn(), true);
    ASSERT_EQ(t2.special(), 255);
    ASSERT_EQ(t2.index(), DerefTableU16S8::ENTRIES_PER_BIN_COUNT - 1);
}

template<std::unsigned_integral TTinyPtr = uint8_t, unsigned STinyPtr = 0>
void TestDereferenceTable() {
    using DerefTable = DerefTable<uint32_t, TTinyPtr, STinyPtr>;
    using TinyPtrT = TinyPtr<TTinyPtr, STinyPtr>;

    constexpr size_t BUCKET_COUNT = 4;

    constexpr auto ENTRY_COUNT = DerefTable::ENTRIES_PER_BIN_COUNT;

    constexpr TTinyPtr SPECIAL_VALUE = STinyPtr == 0 ? 0 : STinyPtr - 1;

    DerefTable deref_table(BUCKET_COUNT);

    ASSERT_EQ(deref_table.size(), 0);
    ASSERT_GE(deref_table.capacity(), BUCKET_COUNT*ENTRY_COUNT);

    for (int i = 0; i < ENTRY_COUNT; ++i) {
        // assert values are linked free list
        auto val = i == ENTRY_COUNT - 1 ? 0 : i + 1;
        ASSERT_EQ(*reinterpret_cast<const TTinyPtr *>(deref_table.dereference(TinyPtrT(i, 0), {0, 0})), val);
        ASSERT_EQ(*reinterpret_cast<const TTinyPtr *>(deref_table.dereference(TinyPtrT(i, 0), {1, 1})), val);
        ASSERT_EQ(*reinterpret_cast<const TTinyPtr *>(deref_table.dereference(TinyPtrT(i, 0), {2, 2})), val);
    }

    std::vector<std::pair<TinyPtrT, uint32_t *> > bin1;
    std::vector<std::pair<TinyPtrT, uint32_t *> > bin2;
    std::vector<std::pair<TinyPtrT, uint32_t *> > bin3;

    for (int i = 0; i < ENTRY_COUNT; ++i) {
        bin1.push_back(deref_table.allocate({0, 0}, SPECIAL_VALUE));
        ASSERT_EQ(deref_table.size(), i + 1);
    }

    // bin 1 is full and returns null tinyptr
    EXPECT_THROW(deref_table.allocate({0, 0}), std::runtime_error);

    for (int i = 0; i < ENTRY_COUNT; ++i) {
        bin2.push_back(deref_table.allocate({0, 1}, SPECIAL_VALUE));
        bin3.push_back(deref_table.allocate({2, 0}, SPECIAL_VALUE));

        ASSERT_EQ(deref_table.size(), ENTRY_COUNT + 2 * (i + 1));
    }

    // bin 2 and 3 are full and return null tinyptr
    EXPECT_THROW(deref_table.allocate({1, 1}), std::runtime_error);
    EXPECT_THROW(deref_table.allocate({2, 2}), std::runtime_error);

    ASSERT_EQ(deref_table.size(), 3 * ENTRY_COUNT);

    // assert bins are stored consecutively in memory
    ASSERT_EQ(bin2[0].second, bin1[ENTRY_COUNT - 1].second + 1);
    ASSERT_EQ(bin3[0].second, bin2[ENTRY_COUNT - 1].second + 1);

    for (int i = 0; i < ENTRY_COUNT; ++i) {
        // assert correct hash function bit
        ASSERT_EQ(bin1[i].first.hash_fn(), 0);
        ASSERT_EQ(bin2[i].first.hash_fn(), 1);
        ASSERT_EQ(bin3[i].first.hash_fn(), 0);

        // assert tinyptr indices
        ASSERT_EQ(bin1[i].first.index(), i);
        ASSERT_EQ(bin2[i].first.index(), i);
        ASSERT_EQ(bin3[i].first.index(), i);

        // assert tinyptr specials
        ASSERT_EQ(bin1[i].first.special(), SPECIAL_VALUE);
        ASSERT_EQ(bin2[i].first.special(), SPECIAL_VALUE);
        ASSERT_EQ(bin3[i].first.special(), SPECIAL_VALUE);

        // assert pointers
        ASSERT_EQ(bin1[i].second, bin1[0].second + i);
        ASSERT_EQ(bin2[i].second, bin2[0].second + i);
        ASSERT_EQ(bin3[i].second, bin3[0].second + i);
    }

    // update values through tinyptr
    for (int i = 0; i < ENTRY_COUNT; ++i) {
        switch (i % 3) {
            case 0: *deref_table.dereference(bin1[i].first, {0, 2}) = i;
                break;
            case 1: *deref_table.dereference(bin2[i].first, {0, 1}) = i;
                break;
            case 2: *deref_table.dereference(bin3[i].first, {2, 0}) = i;
                break;
            default: FAIL() << "unexpected modulo result";
        }
    }

    for (int i = 0; i < ENTRY_COUNT; ++i) {
        uint32_t val1 = 0, val2 = 0;

        switch (i % 3) {
            case 0:
                val1 = *deref_table.dereference(bin1[i].first, {0, 2});
                val2 = *bin1[i].second;
                break;
            case 1:
                val1 = *deref_table.dereference(bin2[i].first, {0, 1});
                val2 = *bin2[i].second;
                break;
            case 2:
                val1 = *deref_table.dereference(bin3[i].first, {2, 0});
                val2 = *bin3[i].second;
                break;
            default: FAIL() << "unexpected modulo result";
        }

        ASSERT_EQ(val1, i);
        ASSERT_EQ(val2, i);
    }

    const int free_count = (ENTRY_COUNT + 1) / 2;

    for (int i = 0; i < free_count; ++i) {
        deref_table.free(bin1[i].first, {0, 0});
        deref_table.free(bin2[i].first, {1, 1});
        deref_table.free(bin3[i].first, {2, 2});
    }

    ASSERT_EQ(deref_table.size(), 3 * ENTRY_COUNT - 3 * free_count);

    for (int i = 0; i < free_count; ++i) {
        deref_table.allocate({0, 1});
        deref_table.allocate({1, 2});
        deref_table.allocate({2, 0});
    }

    ASSERT_EQ(deref_table.size(), 3 * ENTRY_COUNT);
}

TEST(TestTinyPtr, DerefTable) {
    TestDereferenceTable<uint8_t, 0>();
    TestDereferenceTable<uint8_t, 1>();
    TestDereferenceTable<uint8_t, 2>();
    TestDereferenceTable<uint8_t, 3>();
    TestDereferenceTable<uint8_t, 4>();
    TestDereferenceTable<uint8_t, 5>();
    TestDereferenceTable<uint8_t, 6>();
    TestDereferenceTable<uint16_t, 0>();
    TestDereferenceTable<uint16_t, 2>();
    TestDereferenceTable<uint16_t, 4>();
    TestDereferenceTable<uint16_t, 8>();
}

namespace {
    unsigned int worker_thread_count() {
        const unsigned int hw = std::thread::hardware_concurrency();
        return std::max(hw, 4U);
    }

    template<typename F>
    void run_on_threads(unsigned int thread_count, F &&work) {
        std::barrier start_barrier(thread_count);
        std::mutex exc_mutex;
        std::exception_ptr first_exception;

        std::vector<std::thread> threads;
        threads.reserve(thread_count);
        for (unsigned int t = 0; t < thread_count; ++t) {
            threads.emplace_back([&, t] {
                start_barrier.arrive_and_wait();
                try {
                    work(t);
                } catch (...) {
                    std::lock_guard lock(exc_mutex);
                    if (!first_exception)
                        first_exception = std::current_exception();
                }
            });
        }
        for (auto &th: threads)
            th.join();
        if (first_exception)
            std::rethrow_exception(first_exception);
    }

    // Asserts that `ptrs` contains no duplicate pointer values (would indicate free-list corruption
    // handing the same slot to two allocators).
    void ASSERT_ALL_POINTERS_UNIQUE(const std::vector<uint32_t *> &ptrs) {
        std::unordered_set<uint32_t *> seen;
        seen.reserve(ptrs.size());
        for (auto *p: ptrs) {
            ASSERT_TRUE(seen.insert(p).second) << "duplicate pointer returned from allocate: " << p;
        }
    }
} // namespace

// ----------------------------------------------------------------------------
// 1. Concurrent allocate targeting the same (h1, h2) pair — worst-case contention on two buckets.
// ----------------------------------------------------------------------------
TEST(TestTinyPtrThreadSafety, ConcurrentAllocateSameBucketPair) {
    constexpr size_t BUCKETS = 64;
    const unsigned int THREADS = worker_thread_count();
    // Two buckets can hold 2 * ENTRIES_PER_BIN_COUNT items. Divide that budget across THREADS so
    // the test is meaningful on machines with many hardware threads without exceeding capacity.
    const size_t ALLOCS_PER_THREAD = (2 * DerefTable<uint32_t>::ENTRIES_PER_BIN_COUNT) / THREADS;
    ASSERT_GT(ALLOCS_PER_THREAD, 0u);

    DerefTable<uint32_t> table(BUCKETS);

    std::vector<std::vector<std::pair<tnyptr_t, uint32_t *> > > results(THREADS);

    run_on_threads(THREADS, [&](unsigned int t) {
        results[t].reserve(ALLOCS_PER_THREAD);
        for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i) {
            auto p = table.allocate({3, 17});
            *p.second = static_cast<uint32_t>((t << 16) | i);
            results[t].push_back(p);
        }
    });

    ASSERT_EQ(table.size(), THREADS * ALLOCS_PER_THREAD);

    std::vector<uint32_t *> all_ptrs;
    for (auto &r: results)
        for (auto &val: r | std::views::values)
            all_ptrs.push_back(val);

    ASSERT_ALL_POINTERS_UNIQUE(all_ptrs);

    // Every tinyptr must still dereference to the value this thread wrote.
    for (unsigned int t = 0; t < THREADS; ++t) {
        for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i) {
            const auto expected = static_cast<uint32_t>((t << 16) | i);
            ASSERT_EQ(*table.dereference(results[t][i].first, {3, 17}), expected);
        }
    }
}

// ----------------------------------------------------------------------------
// 2. Concurrent allocate on disjoint bucket pairs — tests the parallel-fast-path (no contention).
// ----------------------------------------------------------------------------
TEST(TestTinyPtrThreadSafety, ConcurrentAllocateDisjointBuckets) {
    constexpr size_t BUCKETS = 256;
    constexpr size_t ALLOCS_PER_THREAD = DerefTable<uint32_t>::ENTRIES_PER_BIN_COUNT;
    const unsigned int THREADS = worker_thread_count();
    ASSERT_LE(2 * THREADS, BUCKETS);

    DerefTable<uint32_t> table(BUCKETS);
    std::vector<std::vector<std::pair<tnyptr_t, uint32_t *> > > results(THREADS);

    run_on_threads(THREADS, [&](unsigned int t) {
        const uint64_t h1 = 2 * t; // unique pair per thread
        const uint64_t h2 = 2 * t + 1;
        results[t].reserve(ALLOCS_PER_THREAD);
        for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i) {
            auto p = table.allocate({h1, h2});
            *p.second = static_cast<uint32_t>((t << 16) | i);
            results[t].push_back(p);
        }
    });

    ASSERT_EQ(table.size(), THREADS * ALLOCS_PER_THREAD);

    std::vector<uint32_t *> all_ptrs;
    for (auto &r: results)
        for (auto &val: r | std::views::values)
            all_ptrs.push_back(val);
    ASSERT_ALL_POINTERS_UNIQUE(all_ptrs);

    for (unsigned int t = 0; t < THREADS; ++t) {
        const uint64_t h1 = 2 * t;
        const uint64_t h2 = 2 * t + 1;
        for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i) {
            const auto expected = static_cast<uint32_t>((t << 16) | i);
            ASSERT_EQ(*table.dereference(results[t][i].first, {h1, h2}), expected);
        }
    }
}

// ----------------------------------------------------------------------------
// 3. Concurrent allocate with random hashes — stresses power-of-two-choice across the whole table.
// ----------------------------------------------------------------------------
TEST(TestTinyPtrThreadSafety, ConcurrentAllocateRandomHashes) {
    constexpr size_t BUCKETS = 64;
    constexpr size_t ALLOCS_PER_THREAD = 500;
    const unsigned int THREADS = worker_thread_count();

    DerefTable<uint32_t> table(BUCKETS);

    struct Entry {
        tnyptr_t tp;
        uint32_t *ptr;
        uint64_t h1, h2;
        uint32_t expected;
    };

    std::vector<std::vector<Entry> > results(THREADS);

    run_on_threads(THREADS, [&](unsigned int t) {
        std::mt19937_64 rng(0xC0FFEE ^ t);
        std::uniform_int_distribution<uint64_t> dist;
        results[t].reserve(ALLOCS_PER_THREAD);
        for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i) {
            const uint64_t h1 = dist(rng);
            const uint64_t h2 = dist(rng);
            try {
                auto p = table.allocate({h1, h2});
                const auto expected = static_cast<uint32_t>((t << 16) | (i & 0xFFFF));
                *p.second = expected;
                results[t].push_back({p.first, p.second, h1, h2, expected});
            } catch (const std::runtime_error &) {
                // Unlucky hash pair hit two full buckets; acceptable in this stress test.
            }
        }
    });

    size_t total = 0;
    std::vector<uint32_t *> all_ptrs;
    for (auto &r: results) {
        total += r.size();
        for (auto &e: r)
            all_ptrs.push_back(e.ptr);
    }
    ASSERT_EQ(table.size(), total);
    ASSERT_ALL_POINTERS_UNIQUE(all_ptrs);

    for (auto &r: results)
        for (auto &e: r)
            ASSERT_EQ(*table.dereference(e.tp, {e.h1, e.h2}), e.expected);
}

// ----------------------------------------------------------------------------
// 4. Mixed allocate/free workload — each thread churns its own private working set.
// ----------------------------------------------------------------------------
TEST(TestTinyPtrThreadSafety, ConcurrentAllocateFree) {
    constexpr size_t BUCKETS = 128;
    constexpr size_t ITERATIONS = 2000;
    constexpr size_t LIVE_PER_THREAD = 32;
    const unsigned int THREADS = worker_thread_count();

    DerefTable<uint32_t> table(BUCKETS);

    run_on_threads(THREADS, [&](unsigned int t) {
        std::mt19937_64 rng(0xABCDEF ^ t);
        std::uniform_int_distribution<uint64_t> hash_dist;
        std::uniform_int_distribution<size_t> slot_dist(0, LIVE_PER_THREAD - 1);

        struct LiveSlot {
            tnyptr_t tp;
            uint32_t *ptr{};
            uint64_t h1{}, h2{};
            bool alive = false;
        };
        std::vector<LiveSlot> live(LIVE_PER_THREAD);

        for (size_t it = 0; it < ITERATIONS; ++it) {
            const size_t slot = slot_dist(rng);
            if (live[slot].alive) {
                // Validate the value we previously stored is still intact, then free.
                ASSERT_EQ(*table.dereference(live[slot].tp, {live[slot].h1, live[slot].h2}),
                          static_cast<uint32_t>((t << 16) | slot));
                table.free(live[slot].tp, {live[slot].h1, live[slot].h2});
                live[slot].alive = false;
            } else {
                const uint64_t h1 = hash_dist(rng);
                const uint64_t h2 = hash_dist(rng);
                try {
                    auto p = table.allocate({h1, h2});
                    *p.second = static_cast<uint32_t>((t << 16) | slot);
                    live[slot] = {p.first, p.second, h1, h2, true};
                } catch (const std::runtime_error &) {
                    // table was transiently full; skip
                }
            }
        }

        // Release anything we still hold, so the final size check is deterministic.
        for (auto &s: live)
            if (s.alive)
                table.free(s.tp, {s.h1, s.h2});
    });

    ASSERT_EQ(table.size(), 0u);

    // Fresh allocations must still succeed and be unique after the churn.
    std::vector<uint32_t *> ptrs;
    for (int i = 0; i < 256; ++i) {
        ptrs.push_back(table.allocate({i, i + 1}).second);
    }
    ASSERT_ALL_POINTERS_UNIQUE(ptrs);
}

// ----------------------------------------------------------------------------
// 5. Fill-to-capacity from many threads — exercises the "both buckets full" retry / throw path.
// ----------------------------------------------------------------------------
TEST(TestTinyPtrThreadSafety, ConcurrentFillToCapacity) {
    constexpr size_t BUCKETS = 16; // smallish so we actually hit capacity under contention
    const unsigned int THREADS = worker_thread_count();

    DerefTable<uint32_t> table(BUCKETS);
    const size_t capacity = table.capacity();

    std::atomic<size_t> successful_allocs{0};
    std::vector<std::vector<uint32_t *> > results(THREADS);

    run_on_threads(THREADS, [&](unsigned int t) {
        std::mt19937_64 rng(0x5EED ^ t);
        std::uniform_int_distribution<uint64_t> dist;
        results[t].reserve(capacity / THREADS + 16);
        // Try harder than capacity to guarantee we hit the exhaustion path.
        for (size_t i = 0; i < capacity * 2; ++i) {
            try {
                auto p = table.allocate({dist(rng), dist(rng)});
                *p.second = 0xDEADBEEF;
                results[t].push_back(p.second);
                successful_allocs.fetch_add(1, std::memory_order::relaxed);
            } catch (const std::runtime_error &) {
                // Expected once the table is (near-)full.
            }
        }
    });

    // We must never have over-allocated the table.
    ASSERT_LE(successful_allocs.load(), capacity);
    ASSERT_EQ(table.size(), successful_allocs.load());

    std::vector<uint32_t *> all_ptrs;
    for (auto &r: results)
        all_ptrs.insert(all_ptrs.end(), r.begin(), r.end());
    ASSERT_ALL_POINTERS_UNIQUE(all_ptrs);

    // Every recorded pointer must still carry the sentinel value.
    for (auto *p: all_ptrs)
        ASSERT_EQ(*p, 0xDEADBEEFu);
}

// ----------------------------------------------------------------------------
// 6. Repeated fill-and-drain cycles — hammers the seqlock and surfaces ABA / wraparound bugs.
// ----------------------------------------------------------------------------
TEST(TestTinyPtrThreadSafety, FillAndDrainCycles) {
    constexpr size_t BUCKETS = 32;
    constexpr int CYCLES = 10;
    const unsigned int THREADS = worker_thread_count();

    DerefTable<uint32_t> table(BUCKETS);

    for (int cycle = 0; cycle < CYCLES; ++cycle) {
        std::vector<std::vector<std::pair<tnyptr_t, std::pair<uint64_t, uint64_t> > > > owned(THREADS);

        // Phase 1: fill from all threads
        run_on_threads(THREADS, [&](unsigned int t) {
            std::mt19937_64 rng(static_cast<uint64_t>(cycle) * 1000003 + t);
            std::uniform_int_distribution<uint64_t> dist;
            owned[t].reserve(256);
            for (int i = 0; i < 256; ++i) {
                const uint64_t h1 = dist(rng);
                const uint64_t h2 = dist(rng);
                try {
                    auto p = table.allocate({h1, h2});
                    *p.second = (static_cast<uint32_t>(cycle) << 24) | (t << 16) | i;
                    owned[t].push_back({p.first, {h1, h2}});
                } catch (const std::runtime_error &) {
                    // table full — stop trying for this thread
                    break;
                }
            }
        });

        const size_t post_fill_size = table.size();
        size_t owned_total = 0;
        for (auto &o: owned)
            owned_total += o.size();
        ASSERT_EQ(post_fill_size, owned_total);

        // Phase 2: drain all owned items from all threads
        run_on_threads(THREADS, [&](unsigned int t) {
            for (auto &e: owned[t]) {
                table.free(e.first, {e.second.first, e.second.second});
            }
        });

        ASSERT_EQ(table.size(), 0u);
    }

    // After all the churn, the table must still be fully usable.
    std::vector<uint32_t *> ptrs;
    for (int i = 0; i < 512; ++i)
        ptrs.push_back(table.allocate({i, i * 7 + 1}).second);
    ASSERT_ALL_POINTERS_UNIQUE(ptrs);
}

// ----------------------------------------------------------------------------
// 7. Concurrent dereference-writes on disjoint tinyptrs — validates the lock-free read path.
// Each thread owns a private set of tinyptrs; writes through dereference from multiple threads
// on disjoint slots must never interfere, and the table's meta-state must stay consistent.
// ----------------------------------------------------------------------------
TEST(TestTinyPtrThreadSafety, ConcurrentDereferenceWrites) {
    constexpr size_t BUCKETS = 64;
    constexpr size_t SLOTS_PER_THREAD = 200;
    constexpr size_t WRITES_PER_SLOT = 500;
    const unsigned int THREADS = worker_thread_count();

    DerefTable<uint32_t> table(BUCKETS);

    struct Owned {
        tnyptr_t tp;
        uint32_t *ptr;
        uint64_t h1, h2;
    };

    std::vector<std::vector<Owned> > owned(THREADS);

    // Pre-allocate single-threaded so the lock-free read phase is pure.
    for (unsigned int t = 0; t < THREADS; ++t) {
        std::mt19937_64 rng(0xFEED ^ t);
        std::uniform_int_distribution<uint64_t> dist;
        for (size_t i = 0; i < SLOTS_PER_THREAD; ++i) {
            const uint64_t h1 = dist(rng);
            const uint64_t h2 = dist(rng);
            auto p = table.allocate({h1, h2});
            owned[t].push_back({p.first, p.second, h1, h2});
        }
    }

    run_on_threads(THREADS, [&](unsigned int t) {
        for (size_t w = 0; w < WRITES_PER_SLOT; ++w) {
            for (auto &o: owned[t]) {
                *table.dereference(o.tp, {o.h1, o.h2}) = static_cast<uint32_t>((t << 20) | w);
            }
        }
    });

    // After all threads finish, each thread's last write must be visible on its own slots.
    for (unsigned int t = 0; t < THREADS; ++t) {
        const auto expected = static_cast<uint32_t>((t << 20) | (WRITES_PER_SLOT - 1));
        for (auto &o: owned[t]) {
            ASSERT_EQ(*table.dereference(o.tp, {o.h1, o.h2}), expected);
        }
    }

    ASSERT_EQ(table.size(), THREADS * SLOTS_PER_THREAD);
}

// ----------------------------------------------------------------------------
// 8. Long-running randomized stress across all operations.
// ----------------------------------------------------------------------------
TEST(TestTinyPtrThreadSafety, RandomizedStress) {
    constexpr size_t BUCKETS = 256;
    constexpr size_t OPS_PER_THREAD = 20000;
    const unsigned int THREADS = worker_thread_count();

    DerefTable<uint32_t> table(BUCKETS);
    std::atomic<int64_t> live_counter{0};

    run_on_threads(THREADS, [&](unsigned int t) {
        std::mt19937_64 rng(0xDEADBEEF ^ t);
        std::uniform_int_distribution<uint64_t> hash_dist;
        std::uniform_int_distribution<int> op_dist(0, 99);

        struct LiveSlot {
            tnyptr_t tp;
            uint32_t *ptr;
            uint64_t h1, h2;
            uint32_t value;
        };
        std::vector<LiveSlot> live;
        live.reserve(512);

        for (size_t i = 0; i < OPS_PER_THREAD; ++i) {
            const int op = op_dist(rng);

            if (op < 45 || live.empty()) {
                // allocate
                const uint64_t h1 = hash_dist(rng);
                const uint64_t h2 = hash_dist(rng);
                try {
                    auto p = table.allocate({h1, h2});
                    const auto v = static_cast<uint32_t>((t << 24) | (i & 0x00FFFFFF));
                    *p.second = v;
                    live.push_back({p.first, p.second, h1, h2, v});
                    live_counter.fetch_add(1, std::memory_order::relaxed);
                } catch (const std::runtime_error &) {
                    // full
                }
            } else if (op < 80) {
                // dereference and verify
                std::uniform_int_distribution<size_t> idx_dist(0, live.size() - 1);
                const auto &s = live[idx_dist(rng)];
                ASSERT_EQ(*table.dereference(s.tp, {s.h1, s.h2}), s.value);
            } else {
                // free
                std::uniform_int_distribution<size_t> idx_dist(0, live.size() - 1);
                const size_t idx = idx_dist(rng);
                table.free(live[idx].tp, {live[idx].h1, live[idx].h2});
                live[idx] = live.back();
                live.pop_back();
                live_counter.fetch_sub(1, std::memory_order::relaxed);
            }
        }

        // cleanup
        for (auto &s: live) {
            table.free(s.tp, {s.h1, s.h2});
            live_counter.fetch_sub(1, std::memory_order::relaxed);
        }
    });

    ASSERT_EQ(live_counter.load(), 0);
    ASSERT_EQ(table.size(), 0u);
}
