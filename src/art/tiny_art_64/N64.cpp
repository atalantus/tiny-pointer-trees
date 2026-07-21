#include <assert.h>
#include <algorithm>
#if defined(__AVX512F__) && defined(__AVX512BW__)
#include <immintrin.h>
#endif

#include "N.h"
#include "ArtDerefTables.h"

namespace TINY_ART_64_OLC {
bool N64::isFull() const {
  return count == 64;
}

bool N64::isUnderfull() const {
  return count == 17;
}

void N64::insert(uint8_t key, ArtTinyPtr n) {
  const uint8_t keyByteFlipped = flipSign(key);
#if defined(__AVX512F__) && defined(__AVX512BW__)
  const __mmask64 matches = _mm512_cmplt_epi8_mask(
      _mm512_set1_epi8(keyByteFlipped),
      _mm512_loadu_si512(keys));
  const uint64_t validMask = (uint64_t{1} << count) - 1;
  const uint64_t bitfield = matches & validMask;
  const unsigned pos = bitfield ? ctz(bitfield) : count;
#else
  unsigned pos = 0;
  while (pos < count &&
         static_cast<int8_t>(keys[pos]) <= static_cast<int8_t>(keyByteFlipped)) {
    ++pos;
  }
#endif
  memmove(keys + pos + 1, keys + pos, count - pos);
  memmove(children + pos + 1, children + pos,
          (count - pos) * sizeof(ArtTinyPtr));
  keys[pos] = keyByteFlipped;
  children[pos] = n;
  count++;
}

template <class NODE>
void N64::copyTo(NODE* n) const {
  for (unsigned i = 0; i < count; ++i) {
    n->insert(flipSign(keys[i]), children[i]);
  }
}

bool N64::change(uint8_t key, ArtTinyPtr val) {
  ArtTinyPtr* childPos = const_cast<ArtTinyPtr*>(getChildPos(key));
  assert(childPos != nullptr);
  *childPos = val;
  return true;
}

ArtTinyPtr const* N64::getChildPos(const uint8_t k) const {
#if defined(__AVX512F__) && defined(__AVX512BW__)
  const __mmask64 matches = _mm512_cmpeq_epi8_mask(
      _mm512_set1_epi8(flipSign(k)),
      _mm512_loadu_si512(keys));
  const uint64_t validMask = count == 64
                               ? ~uint64_t{0}
                               : (uint64_t{1} << count) - 1;
  const uint64_t bitfield = matches & validMask;
  return bitfield ? &children[ctz(bitfield)] : nullptr;
#else
  const uint8_t keyByteFlipped = flipSign(k);
  for (unsigned i = 0; i < count; ++i) {
    if (keys[i] == keyByteFlipped) {
      return &children[i];
    }
  }
  return nullptr;
#endif
}

ArtTinyPtr N64::getChild(const uint8_t k) const {
  ArtTinyPtr const* childPos = getChildPos(k);
  return childPos == nullptr ? ArtTinyPtr::null : *childPos;
}

void N64::remove(uint8_t k) {
  ArtTinyPtr const* leafPlace = getChildPos(k);
  assert(leafPlace != nullptr);
  const std::size_t pos = leafPlace - children;
  memmove(keys + pos, keys + pos + 1, count - pos - 1);
  memmove(children + pos, children + pos + 1,
          (count - pos - 1) * sizeof(ArtTinyPtr));
  count--;
  assert(getChild(k) == ArtTinyPtr::null);
}

std::pair<ArtTinyPtr, uint8_t> N64::getAnyChild() const {
  for (unsigned i = 0; i < count; ++i) {
    if (N::isLeaf(children[i])) {
      return {children[i], flipSign(keys[i])};
    }
  }
  return {children[0], flipSign(keys[0])};
}

std::tuple<ArtTinyPtr, uint8_t> N64::getSecondChild(const uint8_t key) const {
  for (unsigned i = 0; i < count; ++i) {
    if (flipSign(keys[i]) != key) {
      return std::make_tuple(children[i], flipSign(keys[i]));
    }
  }
  return std::make_tuple(ArtTinyPtr::null, 0);
}

uint64_t N64::getChildren(uint8_t start, uint8_t end,
                          std::tuple<uint8_t, ArtTinyPtr>*& children,
                          uint32_t& childrenCount) const {
restart:
  bool needRestart = false;
  const uint64_t v = readLockOrRestart(needRestart);
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

std::pair<ArtTinyPtr, N64*> N64::Create(const uint8_t* prefix,
                                        uint32_t prefixLength,
                                        const TinyPtrHashes& h,
                                        ArtDerefTables& deref_tables) {
  auto [tinyPtr, ptr] = deref_tables.n64_deref_table.allocate(h, N64S);
  return {tinyPtr, new(ptr) N64(prefix, prefixLength)};
}
} // namespace TINY_ART_64_OLC