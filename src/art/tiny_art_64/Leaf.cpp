#include "ArtDerefTables.h"
#include "N.h"

namespace TINY_ART_64_OLC {
std::pair<ArtTinyPtr, Leaf*> Leaf::Create(TID tid,
                                          const TinyPtrHashes& h,
                                          ArtDerefTables&
                                          deref_tables) {
  auto [tinyPtr, ptr] = deref_tables.leaf_deref_table.allocate(h, LeafS);
  return {tinyPtr, new(ptr) Leaf(tid)};
}
}