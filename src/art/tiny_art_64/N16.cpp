#include <assert.h>
#include <algorithm>
#include "N.h"
#include "ArtDerefTables.h"
#include <emmintrin.h> // x86 SSE intrinsics

namespace TINY_ART_64_OLC {
bool N16::isFull() const {
  return count == 16;
}

bool N16::isUnderfull() const {
  return false;
}

void N16::insert(uint8_t key, ArtTinyPtr n) {
  uint8_t keyByteFlipped = flipSign(key);
  __m128i cmp = _mm_cmplt_epi8(_mm_set1_epi8(keyByteFlipped),
                               _mm_loadu_si128(
                                   reinterpret_cast<__m128i*>(keys)));
  uint16_t bitfield = _mm_movemask_epi8(cmp) & (0xFFFF >> (16 - count));
  unsigned pos = bitfield ? ctz(bitfield) : count;
  memmove(keys + pos + 1, keys + pos, count - pos);
  memmove(children + pos + 1, children + pos,
          (count - pos) * sizeof(uintptr_t));
  keys[pos] = keyByteFlipped;
  children[pos] = n;
  count++;
}

template <class NODE>
void N16::copyTo(NODE* n) const {
  for (unsigned i = 0; i < count; i++) {
    n->insert(flipSign(keys[i]), children[i]);
  }
}

bool N16::change(uint8_t key, ArtTinyPtr val) {
  ArtTinyPtr* childPos = const_cast<ArtTinyPtr*>(getChildPos(key));
  assert(childPos != nullptr);
  *childPos = val;
  return true;
}

ArtTinyPtr const* N16::getChildPos(const uint8_t k) const {
  __m128i cmp = _mm_cmpeq_epi8(_mm_set1_epi8(flipSign(k)),
                               _mm_loadu_si128(
                                   reinterpret_cast<const __m128i*>(keys)));
  unsigned bitfield = _mm_movemask_epi8(cmp) & ((1 << count) - 1);
  if (bitfield) {
    return &children[ctz(bitfield)];
  } else {
    return nullptr;
  }
}

ArtTinyPtr N16::getChild(const uint8_t k) const {
  ArtTinyPtr const* childPos = getChildPos(k);
  if (childPos == nullptr) {
    return ArtTinyPtr::null;
  } else {
    return *childPos;
  }
}

void N16::remove(uint8_t k) {
  ArtTinyPtr const* leafPlace = getChildPos(k);
  assert(leafPlace != nullptr);
  std::size_t pos = leafPlace - children;
  memmove(keys + pos, keys + pos + 1, count - pos - 1);
  memmove(children + pos, children + pos + 1, (count - pos - 1) * sizeof(N*));
  count--;
  assert(getChild(k) == ArtTinyPtr::null);
}

std::pair<ArtTinyPtr, uint8_t> N16::getAnyChild() const {
  for (int i = 0; i < count; ++i) {
    if (N::isLeaf(children[i])) {
      return {children[i], flipSign(keys[i])};
    }
  }
  return {children[0], flipSign(keys[0])};
}

uint64_t N16::getChildren(uint8_t start, uint8_t end,
                          std::tuple<uint8_t, ArtTinyPtr>*& children,
                          uint32_t& childrenCount) const {
restart:
  bool needRestart = false;
  uint64_t v;
  v = readLockOrRestart(needRestart);
  if (needRestart) goto restart;
  childrenCount = 0;
  auto startPos = getChildPos(start);
  auto endPos = getChildPos(end);
  if (startPos == nullptr) {
    startPos = this->children;
  }
  if (endPos == nullptr) {
    endPos = this->children + (count - 1);
  }
  for (auto p = startPos; p <= endPos; ++p) {
    children[childrenCount] = std::make_tuple(
        flipSign(keys[p - this->children]), *p);
    childrenCount++;
  }
  readUnlockOrRestart(v, needRestart);
  if (needRestart) goto restart;
  return v;
}

std::pair<ArtTinyPtr, N16*> N16::Create(const uint8_t* prefix,
                                        uint32_t prefixLength,
                                        const TinyPtrHashes& h,
                                        ArtDerefTables& deref_tables) {
  auto [tinyPtr, ptr] = deref_tables.n16_deref_table.allocate(h, N16S);
  return {tinyPtr, new(ptr) N16(prefix, prefixLength)};
}
}