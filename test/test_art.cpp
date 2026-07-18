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

#ifdef USE_N64
TEST(TestTinyArt, N64InsertLookupAndRemove) {
  TINY_ART_OLC::N64 node(nullptr, 0);

  for (uint8_t key = 64; key > 0; --key) {
    node.insert(key - 1, TINY_ART_OLC::ArtTinyPtr{key});
  }

  ASSERT_TRUE(node.isFull());
  ASSERT_EQ(node.getCount(), 64);
  for (uint8_t key = 0; key < 64; ++key) {
    EXPECT_EQ(node.getChild(key),
              TINY_ART_OLC::ArtTinyPtr{static_cast<uint8_t>(key + 1)});
  }

  std::array<std::tuple<uint8_t, TINY_ART_OLC::ArtTinyPtr>, 64> children;
  auto* childrenPtr = children.data();
  uint32_t childrenCount = 0;
  node.getChildren(0, 63, childrenPtr, childrenCount);
  ASSERT_EQ(childrenCount, 64);
  for (uint8_t key = 0; key < 64; ++key) {
    EXPECT_EQ(std::get<0>(children[key]), key);
    EXPECT_EQ(std::get<1>(children[key]),
              TINY_ART_OLC::ArtTinyPtr{static_cast<uint8_t>(key + 1)});
  }

  node.change(32, TINY_ART_OLC::ArtTinyPtr{200});
  EXPECT_EQ(node.getChild(32), TINY_ART_OLC::ArtTinyPtr{200});
  node.remove(32);
  EXPECT_EQ(node.getChild(32), TINY_ART_OLC::ArtTinyPtr::null);
  EXPECT_EQ(node.getCount(), 63);
}
#endif

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