#pragma once

#include "N.h"

namespace TINY_ART_256_OLC {
class ArtDerefTables {
public:
  ArtN256DerefTable n256_deref_table;
  ArtLeafDerefTable leaf_deref_table;

  ArtDerefTables(size_t n256_count,
                 size_t leaf_count)
    : n256_deref_table(ArtN256DerefTable::Create(n256_count)),
      leaf_deref_table(ArtLeafDerefTable::Create(leaf_count)) {
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

  void printDerefTableSizes() {
    std::cout << "N256 Deref Table: ";
    n256_deref_table.printDerefTableStats();
    std::cout << "Leaf Deref Table: ";
    leaf_deref_table.printDerefTableStats();
  }
};
} // namespace TINY_ART_256_OLC