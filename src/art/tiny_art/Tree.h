#pragma once

#include "ArtDerefTables.h"
#include "N.h"

using namespace ART;

namespace TINY_ART_OLC {
class Tree {
public:
  using LoadKeyFunction = void (*)(TID tid, Key& key);

private:
  ArtDerefTables deref_tables;

  std::pair<ArtTinyPtr, N256*> const root;

  TID checkKey(const TID tid, const Key& k) const;

  LoadKeyFunction loadKey;

  Epoche epoche{256};

public:
  enum class CheckPrefixResult : uint8_t {
    Match,
    NoMatch,
    OptimisticMatch
  };

  enum class CheckPrefixPessimisticResult : uint8_t {
    Match,
    NoMatch,
  };

  enum class PCCompareResults : uint8_t {
    Smaller,
    Equal,
    Bigger,
  };

  enum class PCEqualsResults : uint8_t {
    BothMatch,
    Contained,
    NoMatch
  };

  static CheckPrefixResult checkPrefix(const N* n, const Key& k, uint32_t& level);

  CheckPrefixPessimisticResult checkPrefixPessimistic(
      std::pair<ArtTinyPtr, N*> n, const Key& k, uint32_t& level,
      uint8_t& nonMatchingKey,
      Prefix& nonMatchingPrefix,
      LoadKeyFunction loadKey, bool& needRestart);

  PCCompareResults checkPrefixCompare(std::pair<ArtTinyPtr, const N*> n, const Key& k,
                                             uint8_t fillKey, uint32_t& level,
                                             LoadKeyFunction loadKey,
                                             bool& needRestart);

  PCEqualsResults checkPrefixEquals(std::pair<ArtTinyPtr, const N*> n, uint32_t& level,
                                           const Key& start, const Key& end,
                                           LoadKeyFunction loadKey,
                                           bool& needRestart);

public:
  Tree(LoadKeyFunction loadKey);

  Tree(const Tree&) = delete;

  Tree(Tree&& t) : root(t.root), loadKey(t.loadKey) {
  }

  ~Tree();

  ThreadInfo getThreadInfo();

  TID lookup(const Key& k, ThreadInfo& threadEpocheInfo) const;

  // bool lookupRange(const Key& start, const Key& end, Key& continueKey,
  //                  TID result[], std::size_t resultLen,
  //                  std::size_t& resultCount,
  //                  ThreadInfo& threadEpocheInfo) const;
  //
  // bool lookupRange(const Key& start, TID result[], std::size_t resultLen,
  //                  std::size_t& resultCount,
  //                  ThreadInfo& threadEpocheInfo) const;

  void insert(const Key& k, TID tid, ThreadInfo& epocheInfo);

  // void remove(const Key& k, TID tid, ThreadInfo& epocheInfo);
};
}