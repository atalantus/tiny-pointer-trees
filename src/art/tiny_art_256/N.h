#pragma once

#include <atomic>
#include <cstring>
#include "Epoche.h"
#include "tiny_ptr/deref_table.h"

using TID = uint64_t;

namespace TINY_ART_256_OLC {
class ArtDerefTables;

class N256;
class Leaf;

/**
* In lowest-byte-first order the bits are grouped as follows:
* | 0 | 1 | 2 3 4 5 6 7 |
* where:
*  - 0 indicates the hash function used
*  - 1 indicate the node type (0: Leaf, 1: n256)
*  - 2-7 indicate the index inside the dereference table's bucket (starting at 1)
*/
using ArtTinyPtr = TinyPtr<uint8_t, 1>;
using ArtN256DerefTable = DerefTable<
  N256, ArtTinyPtr::value_type, ArtTinyPtr::SB>;
using ArtLeafDerefTable = DerefTable<
  Leaf, ArtTinyPtr::value_type, ArtTinyPtr::SB>;
static constexpr uint8_t LeafS = 0;
static constexpr uint8_t N256S = 1;

enum class NTypes : uint8_t {
  N256 = N256S
};

using InitialNode = N256;

static constexpr uint32_t maxStoredPrefixLength = 11;

using Prefix = uint8_t[maxStoredPrefixLength];

/**
 * TODO: Check for thread-local version to avoid shared atomic 64-bit counter.
 *  Non-(fully)-unique IDs should not be a problem here.
 *
 * Generates a unique, well-distributed 64-bit id for a node.
 *
 * A splitmix64 finalizer is applied to a strictly increasing atomic counter.
 * Because splitmix64 is a bijection over the counter sequence the resulting
 * ids are guaranteed to be unique (never colliding) while still being
 * pseudo-randomly distributed across the hash space.
 */
inline uint64_t next_node_id() {
  static std::atomic<uint64_t> counter{0};
  uint64_t z = (counter += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

class LN {
protected:
  LN() = default;

  LN(const LN&) = delete;

  LN(LN&&) = delete;

  //2b type 60b version 1b lock 1b obsolete
  std::atomic<uint64_t> typeVersionLockObsolete{0b100};
  // version 1, unlocked, not obsolete


public:
  bool isLocked(uint64_t version) const;

  void writeLockOrRestart(bool& needRestart);

  void upgradeToWriteLockOrRestart(uint64_t& version, bool& needRestart);

  void writeUnlock();

  uint64_t readLockOrRestart(bool& needRestart) const;

  /**
   * returns true if node hasn't been changed in between
   */
  void checkOrRestart(uint64_t startRead, bool& needRestart) const;

  void readUnlockOrRestart(uint64_t startRead, bool& needRestart) const;

  static bool isObsolete(uint64_t version);

  /**
   * can only be called when node is locked
   */
  void writeUnlockObsolete() {
    typeVersionLockObsolete.fetch_add(0b11);
  }

  static bool isLeaf(const ArtTinyPtr n);
};

static_assert(sizeof(LN) == 8);

class N256 : public LN {
  ArtTinyPtr children[256];

  uint64_t id = next_node_id();

  uint32_t prefixCount = 0;

  uint8_t count = 0;
  Prefix prefix;

public:
  N256(const uint8_t* prefix, uint32_t prefixLength) {
    setPrefix(prefix, prefixLength);
    memset(children, '\0', sizeof(children));
  }

  void insert(uint8_t key, ArtTinyPtr val);

  template <class NODE>
  void copyTo(NODE* n) const;

  bool change(uint8_t key, ArtTinyPtr n);

  ArtTinyPtr getChild(const uint8_t k) const;

  void remove(uint8_t k);

  std::pair<ArtTinyPtr, uint8_t> getAnyChild() const;

  void deleteChildren();

  uint64_t getChildren(uint8_t start, uint8_t end,
                       std::tuple<uint8_t, ArtTinyPtr>*& children,
                       uint32_t& childrenCount) const;

  static std::pair<ArtTinyPtr, N256*> Create(const uint8_t* prefix,
                                             uint32_t prefixLength,
                                             const TinyPtrHashes& h,
                                             ArtDerefTables& deref_tables);

  uint64_t getId() const { return id; }

  uint32_t getCount() const;

  static ArtTinyPtr getChild(const uint8_t k, const N256* node);

  static void insertAndUnlock(N256 *node,
                              uint64_t v,
                              N256 *parentNode, uint64_t parentVersion,
                              uint8_t key,
                              const std::function<ArtTinyPtr(N256* parentNode, uint8_t parentKey)>& generateVal,
                              bool &needRestart);

  static bool change(N256* node, uint8_t key, ArtTinyPtr val);

  static void removeAndUnlock(ArtTinyPtr node, const TinyPtrHashes &h,
                              ArtDerefTables &deref_tables, uint64_t v,
                              uint8_t key, N256 *parentNode,
                              uint64_t parentVersion,
                              bool &needRestart);

  bool hasPrefix() const;

  const uint8_t* getPrefix() const;

  void setPrefix(const uint8_t* prefix, uint32_t length);

  void addPrefixBefore(N256* node, uint8_t key);

  uint32_t getPrefixLength() const;

  static std::pair<ArtTinyPtr, uint8_t> getAnyChild(const N256* n);

  static TID getAnyChildTid(const std::pair<ArtTinyPtr, const N256*>& n,
                            ArtDerefTables& deref_tables, bool& needRestart);

  template <typename curN>
  static void insert(curN *n,
                         uint64_t v,
                         N256 *parentNode,
                         uint64_t parentVersion, uint8_t key,
                         const std::function<ArtTinyPtr(N256* parentNode, uint8_t parentKey)>& generateVal,
                         bool &needRestart);

  template <typename curN>
  static void remove(curN *n,
                              uint64_t v,
                              N256 *parentNode,
                              uint64_t parentVersion,
                              uint8_t key, bool &needRestart);

  static uint64_t getChildren(const N256* node, uint8_t start, uint8_t end,
                              std::tuple<uint8_t, ArtTinyPtr> children[],
                              uint32_t& childrenCount);
};

class Leaf : public LN {
public:
  Leaf(TID value) : value(value) {
  }

  TID value;

  static std::pair<ArtTinyPtr, Leaf*> Create(TID tid,
                                             const TinyPtrHashes& h,
                                             ArtDerefTables& deref_tables);
};
}