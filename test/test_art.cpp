#include "gtest/gtest.h"

#include <string>
#include <vector>

#include "art/ARTSynchronized/OptimisticLockCoupling/Tree.h"
#include "art/tiny_art/Tree.h"

void loadKey(TID tid, Key& key) {
  // Store the key of the tuple into the key vector
  // Implementation is database specific
  key.setKeyLen(sizeof(tid));
  reinterpret_cast<uint64_t*>(&key[0])[0] = __builtin_bswap64(tid);
}

void stringToKey(const std::string& str, Key& key) {
  key.set(str.c_str(), str.size() + 1);
}

static std::vector<std::string> g_keyStrings;

void loadStringKey(TID tid, Key& key) {
  stringToKey(g_keyStrings[tid - 1], key);
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

TEST(TestArt, StringInsertLookupTest) {
  g_keyStrings.clear();
  TINY_ART_OLC::Tree tree(loadStringKey);

  const std::vector<std::string> words = {
      "apple", "app", "apply", "banana", "band",
      "bandana", "a", "hello world", "hello there"};

  auto t = tree.getThreadInfo();

  for (const auto& w : words) {
    g_keyStrings.push_back(w);
    TID id = g_keyStrings.size();
    Key key;
    stringToKey(w, key);
    tree.insert(key, id, t);
  }

  for (uint64_t i = 0; i < words.size(); ++i) {
    Key key;
    stringToKey(words[i], key);
    TID id = tree.lookup(key, t);
    ASSERT_EQ(id, i + 1) << "wrong/missing id for key: '" << words[i] << "'";
  }
}

TEST(TestArt, PathCompressionTest) {
}

TEST(TestArt, LazyExpansionTest) {
}