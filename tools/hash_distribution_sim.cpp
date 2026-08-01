// Offline distribution check for DerefTable hash variants.
//
// Replays the power-of-two-choices placement from DerefTable::allocate_impl
// against synthetic (node id, key byte) pairs and reports the resulting bucket
// load distribution. The question it answers is the only one that matters
// operationally: does a bucket ever exceed ENTRIES_PER_BIN_COUNT slots, i.e.
// would this hash trip abort_overflow at the target load factor?
//
// Hash quality cannot cause incorrectness here -- dereference recomputes the
// same hash -- so this is purely about load balance and capacity.
//
// Build:
//   g++ -O3 -std=c++23 -march=native -Isrc tools/hash_distribution_sim.cpp -o hash_sim
//
// Run (defaults model the leaf deref table: 50M leaves, 31 entries/bucket):
//   ./hash_sim
//   ./hash_sim --items 13596942 --fanout 1,4,16,256

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <string_view>
#include <vector>

#include "tiny_ptr/tiny_ptr.h"  // TinyPtrHashes
#include "tiny_ptr/util.h"      // id_hash, mix64, id_hash_k1/k2
#include "util/primes.hpp"

namespace {

using Hashes = std::pair<uint64_t, uint64_t>;

// Deliberately NOT util.h's id_hash_k1/id_hash_k2. h_proposed below is a frozen
// copy of the hash that was validated by this tool; importing the shipping
// constants would make the drift check in main() tautological, since retuning
// them would silently move both sides of the comparison.
constexpr uint64_t K_A = 0x2545F4914F6CDD1DULL;  // odd
constexpr uint64_t K_B = 0x9E3779B97F4A7C15ULL;  // odd, unrelated

// ---------------------------------------------------------------------------
// Hash variants
// ---------------------------------------------------------------------------

// A: whatever src/tiny_ptr/util.h currently defines, so the sweep always
//    measures the shipping hash rather than a copy of it. main() asserts this
//    still agrees with `h_proposed` -- the variant validated here -- so the two
//    do not need a redundant row each.
inline Hashes h_current(const uint64_t id, const uint8_t shift) {
  return id_hash(id, shift);
}

// B: the hash this tool was used to validate, frozen. Not in the sweep: it is
//    the reference `h_current` is checked against at startup. Both multiplies
//    depend only on `shift`, which is a key byte the caller already holds, so
//    they issue while the load of node->id is still in flight. Only the final
//    xor is on the id-dependent critical path.
inline Hashes h_proposed(const uint64_t id, const uint8_t shift) {
  const uint64_t a = static_cast<uint64_t>(shift) * K_A;
  const uint64_t b = static_cast<uint64_t>(shift) * K_B;
  return {id ^ a, std::rotl(id, 32) ^ b};
}

// C: proposed, with the shift term rotated into the high bits. Only relevant
//    for a reduction that consumes high bits; included so the sweep shows what
//    the placement of the bijective byte does under each reducer.
inline Hashes h_proposed_hi(const uint64_t id, const uint8_t shift) {
  const uint64_t a = static_cast<uint64_t>(shift) * K_A;
  const uint64_t b = static_cast<uint64_t>(shift) * K_B;
  return {id ^ std::rotl(a, 56), std::rotl(id, 32) ^ std::rotl(b, 56)};
}

// D: fallback for the day node ids stop being uniformly random. One avalanche
//    pass instead of two, since the rotation yields the second hash for free.
inline Hashes h_proposed_mix(const uint64_t id, const uint8_t shift) {
  const uint64_t h = mix64(id);
  return {h ^ (static_cast<uint64_t>(shift) * K_A),
          std::rotl(h, 32) ^ (static_cast<uint64_t>(shift) * K_B)};
}

// Control: the body id_hash had before commit b138f5b replaced it, i.e.
// literally `return {id + shift, id - shift};`. (address_hash in util.h still
// has this same shape, but this row is about id_hash's own history.) h1 and h2
// differ by only 2*shift, so both reduce to the same or adjacent buckets and
// the power-of-two-choices degenerates to a single choice. Present so the sweep
// demonstrates it can actually detect a bad hash -- if this row does not
// degrade, the simulation is not measuring anything.
inline Hashes h_legacy(const uint64_t id, const uint8_t shift) {
  return {id + shift, id - shift};
}

struct HashVariant {
  const char* name;
  Hashes (*fn)(uint64_t, uint8_t);
};

constexpr HashVariant kHashes[] = {
    {"legacy(control)", h_legacy},
    {"current", h_current},
    {"proposed-hi", h_proposed_hi},
    {"proposed+mix64", h_proposed_mix},
};

// Cheap stand-in for the row `current` and `proposed` used to occupy jointly:
// if util.h's id_hash ever stops being the validated variant, fail loudly here
// instead of asking someone to notice two identical rows drifting apart.
void assert_shipping_hash_is_the_validated_one() {
  std::mt19937_64 rng(0xC0FFEE);
  for (int i = 0; i < 1'000'000; ++i) {
    const uint64_t id = rng();
    const auto shift = static_cast<uint8_t>(rng());
    if (h_current(id, shift) != h_proposed(id, shift)) {
      std::fprintf(stderr,
                   "util.h id_hash no longer matches the variant validated by "
                   "this tool; re-run the sweep before trusting it\n");
      std::abort();
    }
  }
}

// ---------------------------------------------------------------------------
// Reducers
// ---------------------------------------------------------------------------

struct PrimeReduce {
  primes::Prime prime;

  explicit PrimeReduce(const uint64_t desired) : prime(primes::Prime::pick(desired)) {
    assert(prime.get() <= UINT32_MAX);
  }

  [[nodiscard]] uint32_t buckets() const {
    return static_cast<uint32_t>(prime.get());
  }

  [[nodiscard]] uint32_t operator()(const uint64_t h) const {
    return static_cast<uint32_t>(prime.mod(h));
  }
};

struct MaskReduce {
  uint32_t count;

  // main() rejects a desired size above kMaxBuckets, so bit_ceil cannot exceed
  // 2^31 here and the narrowing is lossless. Without that guard the cast wraps
  // to 0, leaving an empty table behind an all-ones mask.
  explicit MaskReduce(const uint64_t desired)
      : count(static_cast<uint32_t>(std::bit_ceil(desired))) {
    assert(std::bit_ceil(desired) <= UINT32_MAX);
  }

  [[nodiscard]] uint32_t buckets() const { return count; }

  [[nodiscard]] uint32_t operator()(const uint64_t h) const {
    return static_cast<uint32_t>(h & (count - 1));
  }
};

// ---------------------------------------------------------------------------
// Workload
//
// The only correlation in the real key space is that one parent node reuses a
// single id across up to 256 distinct key bytes. Node ids themselves are
// uniform (next_node_id() runs a per-thread counter through SplitMix64), and
// ids from different parents are independent. So the workload is fully
// described by: how many shifts share an id, and how those shifts are drawn.
// ---------------------------------------------------------------------------

// Packed: at the default settings this vector holds ~65M entries, and the
// placement loop is memory-bound, so the 3 bytes of tail padding an aligned
// layout would add cost ~195 MB of traffic per pass.
struct [[gnu::packed]] Item {
  uint32_t parent;
  uint8_t shift;
};

struct Workload {
  std::vector<uint64_t> ids;
  std::vector<Item> items;
};

// Mirrors next_node_id(): one SplitMix64 stream per thread, randomly seeded.
class IdGenerator {
 public:
  IdGenerator(const std::size_t streams, const uint64_t seed) : counters(streams) {
    std::mt19937_64 rng(seed);
    for (auto& counter : counters) counter = rng();
  }

  uint64_t next() {
    uint64_t& counter = counters[cursor];
    cursor = (cursor + 1) % counters.size();
    // Same finalizer next_node_id() applies; mix64 lives in util.h.
    return mix64(counter += 0x9E3779B97F4A7C15ULL);
  }

 private:
  std::vector<uint64_t> counters;
  std::size_t cursor = 0;
};

// A node cannot hold more than one child per key byte, so a parent id is never
// shared by more than 256 shifts.
constexpr uint32_t kMaxFanout = 256;

// free_slots below stores a per-bucket free count, so entries/bucket must fit
// the counter type.
constexpr uint32_t kMaxEpb = UINT16_MAX;

// Bucket indices are uint32_t throughout.
constexpr uint64_t kMaxBuckets = uint64_t{1} << 31;

enum class ShiftMode { Random, Dense };

Workload build_workload(const uint64_t item_count, const uint32_t fanout,
                        const ShiftMode mode, const std::size_t streams,
                        const uint64_t seed) {
  Workload w;
  const uint64_t parents = (item_count + fanout - 1) / fanout;
  w.ids.reserve(parents);
  w.items.reserve(item_count);

  IdGenerator gen(streams, seed);
  std::mt19937_64 rng(seed ^ 0x5DEECE66DULL);

  assert(fanout <= kMaxFanout && "caller must clamp; a key byte holds only 256");

  std::array<uint8_t, kMaxFanout> all_bytes{};
  std::iota(all_bytes.begin(), all_bytes.end(), 0);
  std::array<uint8_t, kMaxFanout> bytes{};

  uint64_t placed = 0;
  for (uint64_t p = 0; p < parents && placed < item_count; ++p) {
    w.ids.push_back(gen.next());
    const auto here =
        static_cast<uint32_t>(std::min<uint64_t>(fanout, item_count - placed));

    if (mode == ShiftMode::Random) {
      // Draw `here` distinct bytes: a node cannot hold two children under the
      // same key byte. Selection order does not matter, items are shuffled below.
      std::sample(all_bytes.begin(), all_bytes.end(), bytes.begin(), here, rng);
    }

    for (uint32_t i = 0; i < here; ++i) {
      w.items.push_back({static_cast<uint32_t>(p),
                         mode == ShiftMode::Random ? bytes[i]
                                                   : static_cast<uint8_t>(i)});
    }
    placed += here;
  }

  // Inserts arrive in random key order, so allocations are not grouped by
  // parent. At fanout 1 every item already carries an independently drawn id,
  // so the sequence is exchangeable and shuffling 65M entries across a
  // multi-hundred-MB buffer would change no measured statistic.
  if (fanout > 1) std::shuffle(w.items.begin(), w.items.end(), rng);
  return w;
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

struct Result {
  uint32_t buckets = 0;
  uint32_t max_load = 0;     // at the provisioned item count
  uint64_t full_buckets = 0; // at the provisioned item count
  uint64_t degenerate = 0;   // h1 and h2 reduced to the same bucket
  uint32_t p99 = 0, p999 = 0;
  double fill = 0.0;         // achieved fill at the provisioned item count
  double fill_at_failure = 0.0;  // fill when a bucket first overflowed
  bool failed = false;           // false => ran out of items before overflowing
  bool reached_target = true;    // false => overflowed before placing them all
};

// Places items until the workload is exhausted, snapshotting the distribution
// once `target` items are in. Placement continues past `target` so the run also
// reports the fill factor at which a bucket first overflows -- the number that
// actually decides how much safety margin a hash gives you, and the only one
// that is fair to compare across reducers with different bucket counts.
template <typename Reduce>
Result simulate(const Workload& w, const HashVariant& hash, const Reduce& reduce,
                const uint32_t epb, const uint64_t target) {
  Result r;
  r.buckets = reduce.buckets();
  const auto capacity = static_cast<double>(r.buckets) * static_cast<double>(epb);

  // uint16_t, not uint8_t: the next tiny pointer width up (TinyPtr<uint16_t,6>)
  // already gives 511 entries per bucket, which a byte counter would silently
  // truncate to 255 and report as overflow at half the real load.
  std::vector<uint16_t> free_slots(r.buckets, static_cast<uint16_t>(epb));

  uint64_t placed = 0;
  bool snapshot_taken = false;
  const auto snapshot = [&] {
    std::vector<uint64_t> hist(epb + 1, 0);
    for (const uint16_t free_count : free_slots) ++hist[epb - free_count];
    r.full_buckets = hist[epb];

    // One prefix-sum walk yields the max load and both percentiles.
    const auto want99 = static_cast<uint64_t>(0.99 * r.buckets);
    const auto want999 = static_cast<uint64_t>(0.999 * r.buckets);
    uint64_t running = 0;
    for (uint32_t load = 0; load <= epb; ++load) {
      if (hist[load]) r.max_load = load;
      running += hist[load];
      if (!r.p99 && running >= want99) r.p99 = load;
      if (!r.p999 && running >= want999) r.p999 = load;
    }

    r.fill = static_cast<double>(placed) / capacity;
    snapshot_taken = true;
  };

  for (const auto& item : w.items) {
    const Hashes h = hash.fn(w.ids[item.parent], item.shift);
    const uint32_t i1 = reduce(h.first);
    const uint32_t i2 = reduce(h.second);
    if (i1 == i2) ++r.degenerate;

    // Matches deref_table.h:352 -- ties go to the first hash.
    const uint32_t chosen = free_slots[i1] >= free_slots[i2] ? i1 : i2;
    if (free_slots[chosen] == 0) {
      // abort_overflow() would fire here.
      r.failed = true;
      r.fill_at_failure = static_cast<double>(placed) / capacity;
      r.reached_target = placed >= target;
      if (!snapshot_taken) snapshot();
      break;
    }
    --free_slots[chosen];
    if (++placed == target) snapshot();
  }

  if (!snapshot_taken) snapshot();
  if (!r.failed) {
    // Ran out of workload first; the true failure point is above this.
    r.fill_at_failure = static_cast<double>(placed) / capacity;
  }
  return r;
}

// ---------------------------------------------------------------------------

uint64_t parse_u64(const std::string_view s, const uint64_t fallback) {
  uint64_t out = 0;
  const auto* end = s.data() + s.size();
  const auto [ptr, ec] = std::from_chars(s.data(), end, out);
  return (ec == std::errc{} && ptr == end) ? out : fallback;
}

std::vector<uint32_t> parse_list(const std::string_view s) {
  std::vector<uint32_t> out;
  std::size_t pos = 0;
  while (pos <= s.size()) {
    const std::size_t comma = s.find(',', pos);
    const std::size_t end = comma == std::string_view::npos ? s.size() : comma;
    if (end > pos) out.push_back(static_cast<uint32_t>(parse_u64(s.substr(pos, end - pos), 1)));
    if (comma == std::string_view::npos) break;
    pos = comma + 1;
  }
  if (out.empty()) out.push_back(1);
  return out;
}

void report(const char* reducer, const HashVariant& hash, const Result& r) {
  char fail[16];
  std::snprintf(fail, sizeof(fail), "%s%.4f%s", r.failed ? "" : ">",
                r.fill_at_failure, r.reached_target ? "" : "!");
  std::printf("  %-15s %-6s %9u  %6.4f %4u %5u %5u %11llu %9s %9llu\n", hash.name,
              reducer, r.buckets, r.fill, r.max_load, r.p999, r.p99,
              static_cast<unsigned long long>(r.full_buckets), fail,
              static_cast<unsigned long long>(r.degenerate));
}

}  // namespace

int main(int argc, char** argv) {
  assert_shipping_hash_is_the_validated_one();

  uint64_t items = 50'000'000;
  uint32_t epb = 31;
  double load_factor = 0.82;
  uint64_t seed = 12345;
  std::size_t streams = 16;
  ShiftMode mode = ShiftMode::Random;
  double overshoot = 1.00;  // fraction of the larger reducer's capacity to generate
  std::vector<uint32_t> fanouts{1, 4, 16, 256};

  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string_view flag = argv[i];
    const std::string_view value = argv[i + 1];
    if (flag == "--items") items = parse_u64(value, items);
    else if (flag == "--epb") epb = static_cast<uint32_t>(parse_u64(value, epb));
    else if (flag == "--load-factor") load_factor = std::strtod(value.data(), nullptr);
    else if (flag == "--seed") seed = parse_u64(value, seed);
    else if (flag == "--streams") streams = parse_u64(value, streams);
    else if (flag == "--fanout") fanouts = parse_list(value);
    else if (flag == "--overshoot") overshoot = std::strtod(value.data(), nullptr);
    else if (flag == "--shift-mode") mode = value == "dense" ? ShiftMode::Dense : ShiftMode::Random;
    else std::fprintf(stderr, "unknown flag: %.*s\n", static_cast<int>(flag.size()), flag.data());
  }

  // Reject rather than silently truncate: every one of these limits used to
  // produce plausible-looking but wrong numbers.
  if (items == 0 || epb == 0 || !(load_factor > 0.0 && load_factor <= 1.0) ||
      !(overshoot > 0.0) || streams == 0) {
    std::fprintf(stderr,
                 "items, epb, streams must be > 0; load-factor in (0,1]; "
                 "overshoot > 0\n");
    return 1;
  }
  if (epb > kMaxEpb) {
    std::fprintf(stderr, "--epb %u exceeds the %u the free-slot counter holds\n",
                 epb, kMaxEpb);
    return 1;
  }
  for (const uint32_t fanout : fanouts) {
    if (fanout == 0 || fanout > kMaxFanout) {
      std::fprintf(stderr,
                   "--fanout %u out of range: a node has at most %u children, "
                   "one per key byte\n",
                   fanout, kMaxFanout);
      return 1;
    }
  }

  // Mirrors DerefTable::Create.
  const uint64_t min_buckets = (items + epb - 1) / epb;
  const auto desired = static_cast<uint64_t>(static_cast<double>(min_buckets) / load_factor);

  if (desired == 0 || desired > kMaxBuckets) {
    std::fprintf(stderr,
                 "%llu buckets needed for these settings; bucket indices are "
                 "uint32_t, so the limit is %llu\n",
                 static_cast<unsigned long long>(desired),
                 static_cast<unsigned long long>(kMaxBuckets));
    return 1;
  }

  const PrimeReduce prime(desired);
  const MaskReduce mask(desired);

  std::printf("items=%llu  entries/bucket=%u  target load factor=%.2f\n",
              static_cast<unsigned long long>(items), epb, load_factor);
  std::printf("buckets: prime=%u (%.1f MiB meta @4B)   pow2=%u (%.1f MiB, +%.1f%%)\n\n",
              prime.buckets(), prime.buckets() * 4 / 1048576.0, mask.buckets(),
              mask.buckets() * 4 / 1048576.0,
              100.0 * (static_cast<double>(mask.buckets()) / prime.buckets() - 1.0));

  // Generate enough to fill the *larger* of the two bucket counts, so both
  // reducers are pushed all the way to their first overflow rather than simply
  // running out of workload (which would make the two look equal).
  const uint64_t larger_capacity =
      static_cast<uint64_t>(std::max(prime.buckets(), mask.buckets())) * epb;
  const auto generated =
      static_cast<uint64_t>(static_cast<double>(larger_capacity) * overshoot);

  for (const uint32_t fanout : fanouts) {
    std::printf("--- fanout %u (%s shifts per parent id) ---\n", fanout,
                mode == ShiftMode::Dense ? "dense" : "distinct random");
    std::printf("  %-15s %-6s %9s  %6s %4s %5s %5s %11s   %6s %10s\n", "hash",
                "reduce", "buckets", "fill", "max", "p99.9", "p99",
                "full buckets", "fail@", "h1==h2");

    const Workload w = build_workload(generated, fanout, mode, streams, seed);
    for (const auto& hash : kHashes) {
      report("prime", hash, simulate(w, hash, prime, epb, items));
      report("pow2", hash, simulate(w, hash, mask, epb, items));
    }
    std::printf("\n");
  }
  std::printf("max = highest bucket load at the provisioned item count (cap %u)\n", epb);
  std::printf("fail@ = fill factor when a bucket first overflowed; '!' = could not "
              "even place the provisioned items\n");
  return 0;
}
