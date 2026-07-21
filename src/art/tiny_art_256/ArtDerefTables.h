#pragma once

#include "N.h"

namespace TINY_ART_256_OLC {
class ArtDerefTables {
public:
  ArtN256DerefTable n256_deref_table;
  ArtLeafDerefTable leaf_deref_table;

  ArtDerefTables()
    : n256_deref_table(ArtN256DerefTable::Create(1024)),
      leaf_deref_table(ArtLeafDerefTable::Create(1024)) {
  }

  explicit ArtDerefTables(size_t count)
    : n256_deref_table(ArtN256DerefTable::Create(count)),
      leaf_deref_table(ArtLeafDerefTable::Create(count)) {
  }

  void free(ArtTinyPtr tinyPtr, TinyPtrHashes h) {
    switch (tinyPtr.special()) {
      case LeafS:
        leaf_deref_table.free(tinyPtr, h);
        break;
      case N256S:
        n256_deref_table.free(tinyPtr, h);
        break;
      default: assert(false);
        __builtin_unreachable();
    }
  }
};
} // namespace TINY_ART_256_OLC