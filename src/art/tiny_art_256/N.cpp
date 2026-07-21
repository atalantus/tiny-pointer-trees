#include <cassert>
#include <algorithm>

#include "N.h"
#include "Leaf.cpp"
#include "N256.cpp"
#include "tiny_ptr/util.h"

namespace TINY_ART_256_OLC {
void LN::writeLockOrRestart(bool& needRestart) {
  uint64_t version;
  version = readLockOrRestart(needRestart);
  if (needRestart) return;

  upgradeToWriteLockOrRestart(version, needRestart);
  if (needRestart) return;
}

void LN::upgradeToWriteLockOrRestart(uint64_t& version, bool& needRestart) {
  if (typeVersionLockObsolete.
    compare_exchange_strong(version, version + 0b10)) {
    version = version + 0b10;
  } else {
    needRestart = true;
  }
}

void LN::writeUnlock() {
  typeVersionLockObsolete.fetch_add(0b10);
}

std::pair<ArtTinyPtr, uint8_t> N256::getAnyChild(const N256* node) {
  auto n = node;
  return n->getAnyChild();
}

bool N256::change(N256* node, uint8_t key, ArtTinyPtr val) {
  auto n = node;
  return n->change(key, val);
}

template <typename curN>
void N256::insert(curN* n,
                  uint64_t v,
                  N256* parentNode,
                  uint64_t parentVersion, uint8_t key,
                  const std::function<ArtTinyPtr(
                      N256* parentNode, uint8_t parentKey)>&
                  generateVal, bool& needRestart) {
  if (parentNode != nullptr) {
    parentNode->readUnlockOrRestart(parentVersion, needRestart);
    if (needRestart) return;
  }
  n->upgradeToWriteLockOrRestart(v, needRestart);
  if (needRestart) return;
  n->insert(key, generateVal(n, key));
  n->writeUnlock();
}

void N256::insertAndUnlock(N256* node,
                           uint64_t v,
                           N256* parentNode,
                           uint64_t parentVersion,
                           uint8_t key,
                           const std::function<ArtTinyPtr(
                               N256* parentNode,
                               uint8_t parentKey)>& generateVal,
                           bool& needRestart) {
  auto n = node;
  insert<N256>(n, v, parentNode, parentVersion, key,
               generateVal, needRestart);
}

inline ArtTinyPtr N256::getChild(const uint8_t k, const N256* node) {
  auto n = node;
  return n->getChild(k);
}

template <typename curN>
void N256::remove(curN* n,
                  uint64_t v, N256* parentNode,
                  uint64_t parentVersion,
                  uint8_t key,
                  bool& needRestart) {
  if (parentNode != nullptr) {
    parentNode->readUnlockOrRestart(parentVersion, needRestart);
    if (needRestart) return;
  }
  n->upgradeToWriteLockOrRestart(v, needRestart);
  if (needRestart) return;

  n->remove(key);
  n->writeUnlock();
}

void N256::removeAndUnlock(ArtTinyPtr node, const TinyPtrHashes& h,
                           ArtDerefTables& deref_tables,
                           uint64_t v, uint8_t key, N256* parentNode,
                           uint64_t parentVersion,
                           bool& needRestart) {
  auto node_ptr = deref_tables.n256_deref_table.dereference(node, h);

  auto n = node_ptr;
  remove<N256>(n, v, parentNode, parentVersion, key,
               needRestart);
}

bool LN::isLocked(uint64_t version) const {
  return ((version & 0b10) == 0b10);
}

uint64_t LN::readLockOrRestart(bool& needRestart) const {
  uint64_t version;
  version = typeVersionLockObsolete.load();
  /*        do {
              version = typeVersionLockObsolete.load();
          } while (isLocked(version));*/
  if (isLocked(version) || isObsolete(version)) {
    needRestart = true;
  }
  return version;
  //uint64_t version;
  //while (isLocked(version)) _mm_pause();
  //return version;
}

bool LN::isObsolete(uint64_t version) {
  return (version & 1) == 1;
}

void LN::checkOrRestart(uint64_t startRead, bool& needRestart) const {
  readUnlockOrRestart(startRead, needRestart);
}

void LN::readUnlockOrRestart(uint64_t startRead, bool& needRestart) const {
  needRestart = (startRead != typeVersionLockObsolete.load());
}

uint32_t N256::getPrefixLength() const {
  return prefixCount;
}

bool N256::hasPrefix() const {
  return prefixCount > 0;
}

uint32_t N256::getCount() const {
  return count;
}

const uint8_t* N256::getPrefix() const {
  return prefix;
}

void N256::setPrefix(const uint8_t* prefix, uint32_t length) {
  if (length > 0) {
    memcpy(this->prefix, prefix, std::min(length, maxStoredPrefixLength));
    prefixCount = length;
  } else {
    prefixCount = 0;
  }
}

void N256::addPrefixBefore(N256* node, uint8_t key) {
  uint32_t prefixCopyCount = std::min(maxStoredPrefixLength,
                                      node->getPrefixLength() + 1);
  memmove(this->prefix + prefixCopyCount, this->prefix,
          std::min(this->getPrefixLength(),
                   maxStoredPrefixLength - prefixCopyCount));
  memcpy(this->prefix, node->prefix,
         std::min(prefixCopyCount, node->getPrefixLength()));
  if (node->getPrefixLength() < maxStoredPrefixLength) {
    this->prefix[prefixCopyCount - 1] = key;
  }
  this->prefixCount += node->getPrefixLength() + 1;
}

bool LN::isLeaf(const ArtTinyPtr n) {
  return n.special() == LeafS;
}

TID N256::getAnyChildTid(const std::pair<ArtTinyPtr, const N256*>& n,
                         ArtDerefTables& deref_tables, bool& needRestart) {
  std::pair<ArtTinyPtr, const N256*> nextNode = n;

  while (true) {
    const N256* node = nextNode.second;
    auto v = node->readLockOrRestart(needRestart);
    if (needRestart) return 0;

    auto anyChild = getAnyChild(node);
    nextNode.first = anyChild.first;
    node->readUnlockOrRestart(v, needRestart);
    if (needRestart) return 0;

    assert(nextNode.first != ArtTinyPtr::null);

    auto next_node_h = id_hash(
        node->getId(),
        anyChild.second);

    if (isLeaf(nextNode.first)) {
      return reinterpret_cast<const Leaf*>(deref_tables.leaf_deref_table.
        dereference(nextNode.first, next_node_h))->value;
    }

    nextNode.second = deref_tables.n256_deref_table.dereference(nextNode.first,next_node_h);
  }
}

uint64_t N256::getChildren(const N256* node, uint8_t start, uint8_t end,
                           std::tuple<uint8_t, ArtTinyPtr> children[],
                           uint32_t& childrenCount) {
  auto n = node;
  return n->getChildren(start, end, children, childrenCount);
}
}