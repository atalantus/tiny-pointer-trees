#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "art/ARTSynchronized/OptimisticLockCoupling/Tree.h"
#include "art/tiny_art/Tree.h"
#include "art/tiny_art_256/Tree.h"
#include "art/tiny_art_64/Tree.h"

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
static std::vector<uint64_t> g_numericKeys;

void loadStringKey(TID tid, Key& key) {
  stringToKey(g_keyStrings[tid - 1], key);
}

void loadNumericKey(TID tid, Key& key) {
  key.setKeyLen(sizeof(tid));
  reinterpret_cast<uint64_t*>(&key[0])[0] =
      __builtin_bswap64(g_numericKeys[tid - 1]);
}

template <typename TArt, unsigned N>
void InsertLookupTest(TArt tree) {
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

TEST(TestArt, InsertLookup10Test) {
  InsertLookupTest<ART_OLC::Tree, 10>(ART_OLC::Tree(loadKey));
  InsertLookupTest<TINY_ART_OLC::Tree, 10>(TINY_ART_OLC::Tree(loadKey, 10));
  InsertLookupTest<TINY_ART_64_OLC::Tree, 10>(
      TINY_ART_64_OLC::Tree(loadKey, 10));
  InsertLookupTest<TINY_ART_256_OLC::Tree, 10>(
      TINY_ART_256_OLC::Tree(loadKey, 10));
}

TEST(TestArt, InsertLookup1000000Test) {
  // InsertLookupTest<ART_OLC::Tree, 1000000>(ART_OLC::Tree(loadKey));
  InsertLookupTest<TINY_ART_OLC::Tree, 1'000'000>(
      TINY_ART_OLC::Tree(loadKey, 1'000'000));
  // InsertLookupTest<TINY_ART_64_OLC::Tree, 1000000>(
      // TINY_ART_64_OLC::Tree(loadKey, 1000000));
  // InsertLookupTest<TINY_ART_256_OLC::Tree, 1000000>(
      // TINY_ART_256_OLC::Tree(loadKey, 1000000));
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

template <typename TArt>
void InsertLookupSharedPrefixTest(TArt tree) {
  auto threadInfo = tree.getThreadInfo();

  for (uint64_t i = 0; i < g_numericKeys.size(); ++i) {
    Key key;
    loadNumericKey(i + 1, key);
    tree.insert(key, i + 1, threadInfo);
  }

  for (uint64_t i = 0; i < g_numericKeys.size(); ++i) {
    Key key;
    loadNumericKey(i + 1, key);
    ASSERT_EQ(tree.lookup(key, threadInfo), i + 1);
  }
}

TEST(TestArt, SharedPrefixChildShiftTest) {
  g_numericKeys.clear();
  const std::array<uint8_t, 17> secondBytes = {
      128, 64, 192, 32, 224, 16, 240, 48, 208,
      80, 176, 96, 160, 112, 144, 0, 255};
  for (const auto secondByte : secondBytes) {
    g_numericKeys.push_back((uint64_t{0xAA} << 56) |
                            (static_cast<uint64_t>(secondByte) << 48));
  }

  InsertLookupSharedPrefixTest<ART_OLC::Tree>(
      ART_OLC::Tree(loadNumericKey));
  InsertLookupSharedPrefixTest<TINY_ART_OLC::Tree>(
      TINY_ART_OLC::Tree(loadNumericKey, g_numericKeys.size()));
  InsertLookupSharedPrefixTest<TINY_ART_64_OLC::Tree>(
      TINY_ART_64_OLC::Tree(loadNumericKey, g_numericKeys.size()));
  InsertLookupSharedPrefixTest<TINY_ART_256_OLC::Tree>(
      TINY_ART_256_OLC::Tree(loadNumericKey, g_numericKeys.size()));
}