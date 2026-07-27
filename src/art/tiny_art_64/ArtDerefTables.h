#pragma once

#include "N.h"

namespace TINY_ART_64_OLC {
class ArtDerefTables {
public:
  ArtN16DerefTable n16_deref_table;
  ArtN64DerefTable n64_deref_table;
  ArtN256DerefTable n256_deref_table;
  ArtLeafDerefTable leaf_deref_table;

  ArtDerefTables(size_t n16_count, size_t n64_count, size_t n256_count,
                 size_t leaf_count)
    : n16_deref_table(ArtN16DerefTable::Create(n16_count)),
      n64_deref_table(ArtN64DerefTable::Create(n64_count)),
      n256_deref_table(ArtN256DerefTable::Create(n256_count)),
      leaf_deref_table(ArtLeafDerefTable::Create(leaf_count)) {
  }

  void free(ArtTinyPtr tinyPtr, TinyPtrHashes h) {
    switch (tinyPtr.special()) {
      case LeafS:
        leaf_deref_table.free(tinyPtr, h);
        break;
      case N64S:
        n64_deref_table.free(tinyPtr, h);
        break;
      case N16S:
        n16_deref_table.free(tinyPtr, h);
        break;
      case N256S:
        n256_deref_table.free(tinyPtr, h);
        break;
      default: assert(false);
        __builtin_unreachable();
    }
  }

  [[nodiscard]] std::pair<ArtTinyPtr, N*> relocate_node(
      std::pair<ArtTinyPtr, N*> node, TinyPtrHashes new_h) {
    switch (node.first.special()) {
      case N64S: {
        auto newNode = n64_deref_table.allocate(new_h, N64S);
        memcpy(newNode.second, node.second, sizeof(N64));
        return newNode;
      }
      case N16S: {
        auto newNode = n16_deref_table.allocate(new_h, N16S);
        memcpy(newNode.second, node.second, sizeof(N16));
        return newNode;
      }
      case N256S: {
        auto newNode = n256_deref_table.allocate(new_h, N256S);
        memcpy(newNode.second, node.second, sizeof(N256));
        return newNode;
      }
      default: assert(false);
        __builtin_unreachable();
    }
  }

  [[nodiscard]] N* dereference(ArtTinyPtr tinyPtr,
                               TinyPtrHashes h) {
    switch (tinyPtr.special()) {
      case N64S:
        return n64_deref_table.dereference(tinyPtr, h);
      case N16S:
        return n16_deref_table.dereference(tinyPtr, h);
      case N256S:
        return n256_deref_table.dereference(tinyPtr, h);
      default: assert(false);
        __builtin_unreachable();
    }
  }

  [[nodiscard]] const N* dereference(ArtTinyPtr tinyPtr,
                                     TinyPtrHashes h) const {
    switch (tinyPtr.special()) {
      case N64S:
        return n64_deref_table.dereference(tinyPtr, h);
      case N16S:
        return n16_deref_table.dereference(tinyPtr, h);
      case N256S:
        return n256_deref_table.dereference(tinyPtr, h);
      default: assert(false);
        __builtin_unreachable();
    }
  }

  void printDerefTableSizes() {
    std::cout << "N16 Deref Table: ";
    n16_deref_table.printDerefTableStats();
    std::cout << "N64 Deref Table: ";
    n64_deref_table.printDerefTableStats();
    std::cout << "N256 Deref Table: ";
    n256_deref_table.printDerefTableStats();
    std::cout << "Leaf Deref Table: ";
    leaf_deref_table.printDerefTableStats();
  }
};
} // namespace TINY_ART_64_OLC