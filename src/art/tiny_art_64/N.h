#pragma once

#include <atomic>
#include <cstring>
#include "Epoche.h"
#include "tiny_ptr/deref_table.h"

using TID = uint64_t;

namespace TINY_ART_64_OLC {
class ArtDerefTables;

class N;
class N16;
class N64;
class N256;
class Leaf;

/**
* In lowest-byte-first order the bits are grouped as follows:
* | 0 | 1 2 | 3 4 5 6 7 |
* where:
*  - 0 indicates the hash function used
*  - 1-2 indicate the node type (0: Leaf, 1: N64, 2: N16, 3: n256)
*  - 3-7 indicate the index inside the dereference table's bucket (starting at 1)
*/
using ArtTinyPtr = TinyPtr<uint8_t, 2>;
using ArtN16DerefTable = DerefTable<N16, ArtTinyPtr::value_type, ArtTinyPtr::SB>
;
using ArtN64DerefTable = DerefTable<N64, ArtTinyPtr::value_type, ArtTinyPtr::SB>
;
using ArtN256DerefTable = DerefTable<
  N256, ArtTinyPtr::value_type, ArtTinyPtr::SB>;
using ArtLeafDerefTable = DerefTable<
  Leaf, ArtTinyPtr::value_type, ArtTinyPtr::SB>;
static constexpr uint8_t LeafS = 0;
static constexpr uint8_t N64S = 1;
static constexpr uint8_t N16S = 2;
static constexpr uint8_t N256S = 3;

enum class NTypes : uint8_t {
  N16 = N16S,
  N64 = N64S,
  N256 = N256S
};

using InitialNode = N16;

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

  LN(const N&) = delete;

  LN(N&&) = delete;

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

class N : public LN {
protected:
  N(NTypes type, const uint8_t* prefix, uint32_t prefixLength) {
    setType(type);
    setPrefix(prefix, prefixLength);
  }

  N(const N&) = delete;

  N(N&&) = delete;

  uint64_t id = next_node_id();

  uint32_t prefixCount = 0;

  uint8_t count = 0;
  Prefix prefix;

  void setType(NTypes type);

  static uint64_t convertTypeToVersion(NTypes type);

public:
  NTypes getType() const;

  uint64_t getId() const { return id; }

  uint32_t getCount() const;

  static ArtTinyPtr getChild(const uint8_t k, const N* node);

  static void insertAndUnlock(N* node, ArtTinyPtr nodeTinyPtr,
                              ArtDerefTables& deref_tables, uint64_t v,
                              N* parentNode, uint64_t parentVersion,
                              uint8_t keyParent,
                              uint8_t key,
                              std::function<ArtTinyPtr(
                                  N* parentNode,
                                  uint8_t parentKey)> generateVal,
                              bool& needRestart,
                              ThreadInfo& threadInfo);

  static bool change(N* node, uint8_t key, ArtTinyPtr val);

  static void removeAndUnlock(ArtTinyPtr node, TinyPtrHashes h,
                              ArtDerefTables& deref_tables, uint64_t v,
                              uint8_t key, N* parentNode,
                              uint64_t parentVersion, uint8_t keyParent,
                              bool& needRestart, ThreadInfo& threadInfo);

  bool hasPrefix() const;

  const uint8_t* getPrefix() const;

  void setPrefix(const uint8_t* prefix, uint32_t length);

  void addPrefixBefore(N* node, uint8_t key);

  uint32_t getPrefixLength() const;

  static std::pair<ArtTinyPtr, uint8_t> getAnyChild(const N* n);

  static TID getAnyChildTid(std::pair<ArtTinyPtr, const N*> n,
                            ArtDerefTables& deref_tables, bool& needRestart);

  template <typename curN, typename biggerN>
  static void insertGrow(curN* n, ArtTinyPtr nodeTinyPtr,
                         ArtDerefTables& deref_tables, uint64_t v,
                         N* parentNode,
                         uint64_t parentVersion, uint8_t keyParent, uint8_t key,
                         std::function<ArtTinyPtr(
                             N* parentNode, uint8_t parentKey)> generateVal,
                         bool& needRestart,
                         ThreadInfo& threadInfo);

  template <typename curN, typename smallerN>
  static void removeAndShrink(curN* n, ArtTinyPtr nodeTinyPtr, TinyPtrHashes h,
                              ArtDerefTables& deref_tables, uint64_t v,
                              N* parentNode,
                              uint64_t parentVersion, uint8_t keyParent,
                              uint8_t key, bool& needRestart,
                              ThreadInfo& threadInfo);

  static uint64_t getChildren(const N* node, uint8_t start, uint8_t end,
                              std::tuple<uint8_t, ArtTinyPtr> children[],
                              uint32_t& childrenCount);
};

class N16 : public N {
public:
  uint8_t keys[16];
  ArtTinyPtr children[16];

  static uint8_t flipSign(uint8_t keyByte) {
    // Flip the sign bit, enables signed SSE comparison of unsigned values, used by Node16
    return keyByte ^ 128;
  }

  static inline unsigned ctz(uint16_t x) {
    // Count trailing zeros, only defined for x>0
#ifdef __GNUC__
    return __builtin_ctz(x);
#else
    // Adapted from Hacker's Delight
    unsigned n = 1;
    if ((x & 0xFF) == 0) {
      n += 8;
      x = x >> 8;
    }
    if ((x & 0x0F) == 0) {
      n += 4;
      x = x >> 4;
    }
    if ((x & 0x03) == 0) {
      n += 2;
      x = x >> 2;
    }
    return n - (x & 1);
#endif
  }

  ArtTinyPtr const* getChildPos(const uint8_t k) const;

public:
  N16(const uint8_t* prefix, uint32_t prefixLength) : N(NTypes::N16, prefix,
    prefixLength) {
    memset(keys, 0, sizeof(keys));
    memset(children, 0, sizeof(children));
  }

  void insert(uint8_t key, ArtTinyPtr n);

  template <class NODE>
  void copyTo(NODE* n) const;

  bool change(uint8_t key, ArtTinyPtr val);

  ArtTinyPtr getChild(const uint8_t k) const;

  void remove(uint8_t k);

  std::pair<ArtTinyPtr, uint8_t> getAnyChild() const;

  bool isFull() const;

  bool isUnderfull() const;

  void deleteChildren();

  uint64_t getChildren(uint8_t start, uint8_t end,
                       std::tuple<uint8_t, ArtTinyPtr>*& children,
                       uint32_t& childrenCount) const;

  static std::pair<ArtTinyPtr, N16*> Create(const uint8_t* prefix,
                                            uint32_t prefixLength,
                                            const TinyPtrHashes& h,
                                            ArtDerefTables& deref_tables);
};

class N64 : public N {
public:
  uint8_t keys[64];
  ArtTinyPtr children[64];

  static uint8_t flipSign(uint8_t keyByte) {
    return keyByte ^ 128;
  }

  static inline unsigned ctz(uint64_t x) {
#ifdef __GNUC__
    return __builtin_ctzll(x);
#else
    unsigned n = 0;
    while ((x & 1) == 0) {
      ++n;
      x >>= 1;
    }
    return n;
#endif
  }

  ArtTinyPtr const* getChildPos(uint8_t k) const;

  N64(const uint8_t* prefix, uint32_t prefixLength) : N(
      NTypes::N64,
      prefix,
      prefixLength) {
    memset(keys, 0, sizeof(keys));
    memset(children, 0, sizeof(children));
  }

  void insert(uint8_t key, ArtTinyPtr n);

  template <class NODE>
  void copyTo(NODE* n) const;

  bool change(uint8_t key, ArtTinyPtr val);

  ArtTinyPtr getChild(uint8_t k) const;

  void remove(uint8_t k);

  std::pair<ArtTinyPtr, uint8_t> getAnyChild() const;

  std::tuple<ArtTinyPtr, uint8_t> getSecondChild(uint8_t key) const;

  bool isFull() const;

  bool isUnderfull() const;

  void deleteChildren();

  uint64_t getChildren(uint8_t start, uint8_t end,
                       std::tuple<uint8_t, ArtTinyPtr>*& children,
                       uint32_t& childrenCount) const;

  static std::pair<ArtTinyPtr, N64*> Create(const uint8_t* prefix,
                                            uint32_t prefixLength,
                                            const TinyPtrHashes& h,
                                            ArtDerefTables& deref_tables);
};

class N256 : public N {
  ArtTinyPtr children[256];

public:
  N256(const uint8_t* prefix, uint32_t prefixLength) : N(NTypes::N256, prefix,
    prefixLength) {
    memset(children, '\0', sizeof(children));
  }

  void insert(uint8_t key, ArtTinyPtr val);

  template <class NODE>
  void copyTo(NODE* n) const;

  bool change(uint8_t key, ArtTinyPtr n);

  ArtTinyPtr getChild(const uint8_t k) const;

  void remove(uint8_t k);

  std::pair<ArtTinyPtr, uint8_t> getAnyChild() const;

  bool isFull() const;

  bool isUnderfull() const;

  void deleteChildren();

  uint64_t getChildren(uint8_t start, uint8_t end,
                       std::tuple<uint8_t, ArtTinyPtr>*& children,
                       uint32_t& childrenCount) const;

  static std::pair<ArtTinyPtr, N256*> Create(const uint8_t* prefix,
                                             uint32_t prefixLength,
                                             const TinyPtrHashes& h,
                                             ArtDerefTables& deref_tables);
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