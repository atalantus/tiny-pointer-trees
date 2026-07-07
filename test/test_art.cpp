#include "gtest/gtest.h"

#include "art/ARTSynchronized/OptimisticLockCoupling/Tree.h"
#include "art/tiny_art/Tree.h"

void loadKey(TID tid, Key& key) {
  // Store the key of the tuple into the key vector
  // Implementation is database specific
  key.setKeyLen(sizeof(tid));
  reinterpret_cast<uint64_t*>(&key[0])[0] = __builtin_bswap64(tid);
}

template <typename TArt, unsigned N>
void InsertLookupTest() {
  TArt tree(loadKey);

  std::array<uint64_t, N> keys;
  for (uint64_t i = 0; i < N; ++i) {
    keys[i] = i + 1;
  }

  auto t = tree.getThreadInfo();

  // insert
  for (int i = 0; i < N; ++i) {
    Key key;
    loadKey(keys[i], key);
    tree.insert(key, keys[i], t);
  }

  // lookup
  for (uint64_t i = 0; i < N; ++i) {
    Key key;
    loadKey(keys[i], key);
    auto val = tree.lookup(key, t);
    ASSERT_EQ(val, keys[i]);
  }
}

TEST(TestArt, InsertLookupTest) {
  InsertLookupTest<ART_OLC::Tree, 10>();
  InsertLookupTest<TINY_ART_OLC::Tree, 10>();
}

TEST(TestArt, PathCompressionTest) {
}

TEST(TestArt, LazyExpansionTest) {
}