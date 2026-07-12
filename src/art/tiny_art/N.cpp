#include <assert.h>
#include <algorithm>

#include "N.h"
#include "Leaf.cpp"
#include "N4.cpp"
#include "N16.cpp"
#include "N256.cpp"
#include "tiny_ptr/util.h"

namespace TINY_ART_OLC {
void N::setType(NTypes type) {
  typeVersionLockObsolete.fetch_add(convertTypeToVersion(type));
}

uint64_t N::convertTypeToVersion(NTypes type) {
  return (static_cast<uint64_t>(type) << 62);
}

NTypes N::getType() const {
  return static_cast<NTypes>(typeVersionLockObsolete.load(
                                 std::memory_order_relaxed) >> 62);
}

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

std::pair<ArtTinyPtr, uint8_t> N::getAnyChild(const N* node) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<const N4*>(node);
      return n->getAnyChild();
    }
    case NTypes::N16: {
      auto n = static_cast<const N16*>(node);
      return n->getAnyChild();
    }
    case NTypes::N256: {
      auto n = static_cast<const N256*>(node);
      return n->getAnyChild();
    }
  }
  assert(false);
  __builtin_unreachable();
}

bool N::change(N* node, uint8_t key, ArtTinyPtr val) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<N4*>(node);
      return n->change(key, val);
    }
    case NTypes::N16: {
      auto n = static_cast<N16*>(node);
      return n->change(key, val);
    }
    case NTypes::N256: {
      auto n = static_cast<N256*>(node);
      return n->change(key, val);
    }
  }
  assert(false);
  __builtin_unreachable();
}

template <typename curN, typename biggerN>
void N::insertGrow(curN* n, ArtTinyPtr nodeTinyPtr,
                   ArtDerefTables& deref_tables, uint64_t v, N* parentNode,
                   uint64_t parentVersion, uint8_t keyParent, uint8_t key,
                   std::function<ArtTinyPtr(N* parentNode, uint8_t parentKey)> generateVal, bool& needRestart,
                   ThreadInfo& threadInfo) {
  if (!n->isFull()) {
    if (parentNode != nullptr) {
      parentNode->readUnlockOrRestart(parentVersion, needRestart);
      if (needRestart) return;
    }
    n->upgradeToWriteLockOrRestart(v, needRestart);
    if (needRestart) return;
    n->insert(key, generateVal(n, key));
    n->writeUnlock();
    return;
  }

  parentNode->upgradeToWriteLockOrRestart(parentVersion, needRestart);
  if (needRestart) return;

  n->upgradeToWriteLockOrRestart(v, needRestart);
  if (needRestart) {
    parentNode->writeUnlock();
    return;
  }

  auto nBig = biggerN::Create(n->getPrefix(), n->getPrefixLength(),
                              id_hash(parentNode->getId(), keyParent),
                              deref_tables);
  nBig.second->id = n->id;
  n->copyTo(nBig.second);
  nBig.second->insert(key, generateVal(nBig.second, key));

  N::change(parentNode, keyParent, nBig.first);

  n->writeUnlockObsolete();
  threadInfo.getEpoche().markNodeForDeletion(
      nodeTinyPtr, id_hash(parentNode->getId(), keyParent), threadInfo);
  parentNode->writeUnlock();
}

void N::insertAndUnlock(N* node, ArtTinyPtr nodeTinyPtr,
                        ArtDerefTables& deref_tables, uint64_t v, N* parentNode,
                        uint64_t parentVersion, uint8_t keyParent, uint8_t key,
                        std::function<ArtTinyPtr(N* parentNode, uint8_t parentKey)> generateVal,
                        bool& needRestart,
                        ThreadInfo& threadInfo) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<N4*>(node);
      insertGrow<N4, N16>(n, nodeTinyPtr, deref_tables, v, parentNode,
                          parentVersion, keyParent,
                          key, generateVal, needRestart,
                          threadInfo);
      break;
    }
    case NTypes::N16: {
      auto n = static_cast<N16*>(node);
      insertGrow<N16, N256>(n, nodeTinyPtr, deref_tables, v, parentNode,
                            parentVersion, keyParent,
                            key, generateVal,
                            needRestart, threadInfo);
      break;
    }
    case NTypes::N256: {
      auto n = static_cast<N256*>(node);
      insertGrow<N256, N256>(n, nodeTinyPtr, deref_tables, v, parentNode,
                             parentVersion, keyParent,
                             key, generateVal,
                             needRestart, threadInfo);
      break;
    }
    default:
      assert(false);
      __builtin_unreachable();
  }
}

inline ArtTinyPtr N::getChild(const uint8_t k, const N* node) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<const N4*>(node);
      return n->getChild(k);
    }
    case NTypes::N16: {
      auto n = static_cast<const N16*>(node);
      return n->getChild(k);
    }
    case NTypes::N256: {
      auto n = static_cast<const N256*>(node);
      return n->getChild(k);
    }
  }
  assert(false);
  __builtin_unreachable();
}

template <typename curN, typename smallerN>
void N::removeAndShrink(curN* n, ArtTinyPtr nodeTinyPtr, TinyPtrHashes h,
                        ArtDerefTables& deref_tables,
                        uint64_t v, N* parentNode,
                        uint64_t parentVersion, uint8_t keyParent, uint8_t key,
                        bool& needRestart, ThreadInfo& threadInfo) {
  if (!n->isUnderfull() || parentNode == nullptr) {
    if (parentNode != nullptr) {
      parentNode->readUnlockOrRestart(parentVersion, needRestart);
      if (needRestart) return;
    }
    n->upgradeToWriteLockOrRestart(v, needRestart);
    if (needRestart) return;

    n->remove(key);
    n->writeUnlock();
    return;
  }
  parentNode->upgradeToWriteLockOrRestart(parentVersion, needRestart);
  if (needRestart) return;

  n->upgradeToWriteLockOrRestart(v, needRestart);
  if (needRestart) {
    parentNode->writeUnlock();
    return;
  }

  auto nSmall = smallerN::Create(n->getPrefix(), n->getPrefixLength(), h,
                                 deref_tables);

  nSmall.second->id = n->id;
  n->copyTo(nSmall.second);
  nSmall.second->remove(key);
  N::change(parentNode, keyParent, nSmall.first);

  n->writeUnlockObsolete();
  threadInfo.getEpoche().markNodeForDeletion(nodeTinyPtr, h, threadInfo);
  parentNode->writeUnlock();
}

void N::removeAndUnlock(ArtTinyPtr node, TinyPtrHashes h,
                        ArtDerefTables& deref_tables,
                        uint64_t v, uint8_t key, N* parentNode,
                        uint64_t parentVersion, uint8_t keyParent,
                        bool& needRestart, ThreadInfo& threadInfo) {
  auto node_ptr = deref_tables.dereference(node, h);

  switch (node.special()) {
    case N4S: {
      auto n = static_cast<N4*>(node_ptr);
      removeAndShrink<N4, N4>(n, node, h, deref_tables, v, parentNode,
                              parentVersion, keyParent, key,
                              needRestart, threadInfo);
      break;
    }
    case N16S: {
      auto n = static_cast<N16*>(node_ptr);
      removeAndShrink<N16, N4>(n, node, h, deref_tables, v, parentNode,
                               parentVersion, keyParent, key,
                               needRestart, threadInfo);
      break;
    }
    case N256S: {
      auto n = static_cast<N256*>(node_ptr);
      removeAndShrink<N256, N16>(n, node, h, deref_tables, v, parentNode,
                                 parentVersion, keyParent,
                                 key, needRestart, threadInfo);
      break;
    }
    default:
      assert(false);
      __builtin_unreachable();
  }
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

uint32_t N::getPrefixLength() const {
  return prefixCount;
}

bool N::hasPrefix() const {
  return prefixCount > 0;
}

uint32_t N::getCount() const {
  return count;
}

const uint8_t* N::getPrefix() const {
  return prefix;
}

void N::setPrefix(const uint8_t* prefix, uint32_t length) {
  if (length > 0) {
    memcpy(this->prefix, prefix, std::min(length, maxStoredPrefixLength));
    prefixCount = length;
  } else {
    prefixCount = 0;
  }
}

void N::addPrefixBefore(N* node, uint8_t key) {
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

std::tuple<ArtTinyPtr, uint8_t> N::getSecondChild(N* node, const uint8_t key) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<N4*>(node);
      return n->getSecondChild(key);
    }
    default: {
      assert(false);
      __builtin_unreachable();
    }
  }
}

TID N::getAnyChildTid(std::pair<ArtTinyPtr, const N*> n,
                      ArtDerefTables& deref_tables, bool& needRestart) {
  std::pair<ArtTinyPtr, const N*> nextNode = n;

  while (true) {
    const N* node = nextNode.second;
    auto v = node->readLockOrRestart(needRestart);
    if (needRestart) return 0;

    auto anyChild = getAnyChild(node);
    nextNode.first = anyChild.first;
    node->readUnlockOrRestart(v, needRestart);
    if (needRestart) return 0;

    assert(nextNode.first != ArtTinyPtr::null);

    nextNode.second = deref_tables.dereference(nextNode.first,
                                               id_hash(
                                                   node->getId(),
                                                   anyChild.second));

    if (isLeaf(nextNode.first)) {
      return reinterpret_cast<const Leaf*>(nextNode.second)->value;
    }
  }
}

uint64_t N::getChildren(const N* node, uint8_t start, uint8_t end,
                        std::tuple<uint8_t, ArtTinyPtr> children[],
                        uint32_t& childrenCount) {
  switch (node->getType()) {
    case NTypes::N4: {
      auto n = static_cast<const N4*>(node);
      return n->getChildren(start, end, children, childrenCount);
    }
    case NTypes::N16: {
      auto n = static_cast<const N16*>(node);
      return n->getChildren(start, end, children, childrenCount);
    }
    case NTypes::N256: {
      auto n = static_cast<const N256*>(node);
      return n->getChildren(start, end, children, childrenCount);
    }
  }
  assert(false);
  __builtin_unreachable();
}
}