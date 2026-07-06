#include <assert.h>
#include <algorithm>
#include "N.h"
#include "ArtDerefTables.h"

namespace TINY_ART_OLC {
bool N256::isFull() const {
  return false;
}

bool N256::isUnderfull() const {
  return count == 37;
}

void N256::insert(uint8_t key, ArtTinyPtr val) {
  children[key] = val;
  count++;
}

template <class NODE>
void N256::copyTo(NODE* n) const {
  for (int i = 0; i < 256; ++i) {
    if (children[i] != ArtTinyPtr::null) {
      n->insert(i, children[i]);
    }
  }
}

bool N256::change(uint8_t key, ArtTinyPtr n) {
  children[key] = n;
  return true;
}

ArtTinyPtr N256::getChild(const uint8_t k) const {
  return children[k];
}

void N256::remove(uint8_t k) {
  children[k] = ArtTinyPtr::null;
  count--;
}

std::pair<ArtTinyPtr, uint8_t> N256::getAnyChild() const {
  std::pair<ArtTinyPtr, uint8_t> anyChild = {ArtTinyPtr::null, 0};
  for (uint64_t i = 0; i < 256; ++i) {
    if (children[i] != ArtTinyPtr::null) {
      if (N::isLeaf(children[i])) {
        return {children[i], i};
      } else {
        anyChild = {children[i], i};
      }
    }
  }
  return anyChild;
}

uint64_t N256::getChildren(uint8_t start, uint8_t end,
                           std::tuple<uint8_t, ArtTinyPtr>*& children,
                           uint32_t& childrenCount) const {
restart:
  bool needRestart = false;
  uint64_t v;
  v = readLockOrRestart(needRestart);
  if (needRestart) goto restart;
  childrenCount = 0;
  for (unsigned i = start; i <= end; i++) {
    if (this->children[i] != ArtTinyPtr::null) {
      children[childrenCount] = std::make_tuple(i, this->children[i]);
      childrenCount++;
    }
  }
  readUnlockOrRestart(v, needRestart);
  if (needRestart) goto restart;
  return v;
}

std::pair<ArtTinyPtr, N256*> N256::Create(const uint8_t* prefix,
                                          uint32_t prefixLength,
                                          const TinyPtrHashes& h,
                                          ArtDerefTables& deref_tables) {
  auto [tinyPtr, ptr] = deref_tables.n256_deref_table.allocate(h, N256S);
  return {tinyPtr, new(ptr) N256(prefix, prefixLength)};
}
}