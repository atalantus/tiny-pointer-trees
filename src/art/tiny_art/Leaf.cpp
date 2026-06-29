#include "ArtDerefTables.h"
#include "N.h"

namespace TINY_ART_OLC {
std::pair<ArtTinyPtr, Leaf*> Leaf::Create(TID tid,
                                          const TinyPtrHashes& h,
                                          TINY_ART_OLC::ArtDerefTables&
                                          deref_tables) {
  auto [tinyPtr, ptr] = deref_tables.n256_deref_table.allocate(h, N256S);
  return {tinyPtr, new(ptr) Leaf(tid)};
}
}