#pragma once

#include "N.h"

namespace TINY_ART_OLC {
class ArtDerefTables {
public:
  ArtN4DerefTable n4_deref_table;
  ArtN16DerefTable n16_deref_table;
  ArtN256DerefTable n256_deref_table;
  ArtLeafDerefTable leaf_deref_table;

  ArtDerefTables() : n4_deref_table(ArtN4DerefTable::Create(1024)),
                     n16_deref_table(ArtN16DerefTable::Create(1024)),
                     n256_deref_table(ArtN256DerefTable::Create(1024)),
                     leaf_deref_table(ArtLeafDerefTable::Create(1024)) {
  }

  ArtDerefTables(size_t n4_count, size_t n16_count, size_t n256_count,
                 size_t leaf_count)
    : n4_deref_table(ArtN4DerefTable::Create(n4_count)),
      n16_deref_table(ArtN16DerefTable::Create(n16_count)),
      n256_deref_table(ArtN256DerefTable::Create(n256_count)),
      leaf_deref_table(ArtLeafDerefTable::Create(leaf_count)) {
  }

  std::pair<ArtTinyPtr, void*> move(std::pair<ArtTinyPtr, void*> object,
                                    TinyPtrHashes h, TinyPtrHashes new_h) {
    const auto move_in = [&](auto& table) -> std::pair<ArtTinyPtr, void*> {
      using T = std::remove_reference_t<decltype(table)>::ObjectT;
      auto res = table.move({object.first, static_cast<T*>(object.second)}, h,
                            new_h);
      return {res.first, res.second};
    };

    switch (object.first.special()) {
      case LeafS:
        return move_in(leaf_deref_table);
      case N4S:
        return move_in(n4_deref_table);
      case N16S:
        return move_in(n16_deref_table);
      case N256S:
        return move_in(n256_deref_table);
      default: assert(false);
        __builtin_unreachable();
    }
  }

  [[nodiscard]] N* dereference(ArtTinyPtr tinyPtr,
                               std::pair<uint64_t, uint64_t> h) {
    switch (tinyPtr.special()) {
      case N4S:
        return n4_deref_table.dereference(tinyPtr, h);
      case N16S:
        return n16_deref_table.dereference(tinyPtr, h);
      case N256S:
        return n256_deref_table.dereference(tinyPtr, h);
      default: assert(false);
        __builtin_unreachable();
    }
  }

  [[nodiscard]] const N* dereference(ArtTinyPtr tinyPtr,
                                     std::pair<uint64_t, uint64_t> h) const {
    switch (tinyPtr.special()) {
      case N4S:
        return n4_deref_table.dereference(tinyPtr, h);
      case N16S:
        return n16_deref_table.dereference(tinyPtr, h);
      case N256S:
        return n256_deref_table.dereference(tinyPtr, h);
      default: assert(false);
        __builtin_unreachable();
    }
  }
};
} // namespace TINY_ART_OLC