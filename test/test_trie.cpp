#include "test_words.h"
#include "trie/tiny_trie.h"
#include "trie/tiny_value_trie.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <numeric>

#include "trie/tiny_trie_2.h"

template <typename TTrie>
void test_trie() {
  auto testWords = LoadTestWords();
  auto trie      = TTrie::template BulkCreateUnsorted<std::string>(testWords);

  std::cout << "Number of words: " << trie.size() << "\n";
  std::cout << "Trie node count: " << trie.node_count() << "\n";

  for (const auto& test_word : testWords) {
    ASSERT_TRUE(trie.contains(test_word));
  }

  ASSERT_EQ(testWords.size(), trie.size());

  for (const auto & testWord : testWords) {
    ASSERT_TRUE(trie.contains(testWord));
    ASSERT_TRUE(trie.remove(testWord));
    ASSERT_FALSE(trie.contains(testWord));
  }

  ASSERT_EQ(trie.size(), 0);
}

TEST(TestTrie, TinyTrie) {
 test_trie<TinyTrie>();
}

TEST(TestTrie, TinyTrie2) {
  test_trie<TinyTrie2>();
}

TEST(TestTrie, TinyValueTrie) {
  TinyValueTrie<uint32_t> trie(1'028'663);
  const auto testWords = LoadTestWords();

  ASSERT_EQ(trie.size(), 0);

  for (size_t i = 0; i < testWords.size(); ++i) {
    ASSERT_TRUE(trie.set(testWords[i], i));
    ASSERT_EQ(trie.size(), i + 1);
  }

  std::cout << "Number of words: " << trie.size() << "\n";
  std::cout << "Trie node count: " << trie.node_count() << "\n";

  for (size_t i = 0; i < testWords.size(); ++i) {
    auto val = trie.get(testWords[i]);
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val.value(), i);

    trie.remove(testWords[i]);
  }

  for (const auto & testWord : testWords) {
    auto val = trie.get(testWord);
    ASSERT_FALSE(val.has_value());
  }

  ASSERT_EQ(trie.size(), 0);
}

TEST(TestTrie, TrieUtilNodeCountsSmall) {
  /*
   *           ""
   *        /      \
   *     "a"        "b"
   *    /   \        |
   * "ab"  "ac"    "bc"
   *                 |
   *               "bca"
   */
  const std::vector<std::string> testWords{"", "a", "ab", "ac", "b", "bc", "bca"};
  const auto node_counts = compute_node_counts(testWords);

  for (int i = 0; i < 256; ++i) {
    switch (i) {
      case 0: ASSERT_EQ(node_counts[i], 3); break;
      case 1: ASSERT_EQ(node_counts[i], 2); break;
      case 2: ASSERT_EQ(node_counts[i], 2); break;
      default: ASSERT_EQ(node_counts[i], 0); break;
    }
  }

  const auto trie = TinyTrie::BulkCreate<std::string>(testWords);
  ASSERT_EQ(testWords.size(), trie.size());
  ASSERT_EQ(trie.node_count(), std::reduce(node_counts.begin(), node_counts.end()) - node_counts[0]);
}

TEST(TestTrie, TrieUtilNodeCountsSmallWithDuplicates) {
  /*
   *              ""
   *        /      |      \
   *     "a"      "b"      "c"
   *    /   \      |
   * "ab"  "ac"   "bc"
   *              /  \
   *          "bca" "bcb"
   */
  const std::vector<std::string> testWords{"", "a", "a", "ab", "ac", "ac", "b", "bca", "bca", "bcb", "c"};
  const auto node_counts = compute_node_counts(testWords);

  for (int i = 0; i < 256; ++i) {
    switch (i) {
      case 0: ASSERT_EQ(node_counts[i], 5); break;
      case 1: ASSERT_EQ(node_counts[i], 1); break;
      case 2: ASSERT_EQ(node_counts[i], 2); break;
      case 3: ASSERT_EQ(node_counts[i], 1); break;
      default: ASSERT_EQ(node_counts[i], 0); break;
    }
  }

  const auto trie = TinyTrie::BulkCreate<std::string>(testWords);
  ASSERT_EQ(trie.node_count(), std::reduce(node_counts.begin(), node_counts.end()) - node_counts[0]);
}

TEST(TestTrie, TrieUtilNodeCountsFull) {
  auto testWords = LoadTestWords();
  std::ranges::sort(testWords);
  const auto node_counts = compute_node_counts(testWords);

  for (int i = 0; i < 257; ++i) {
    if (node_counts[i] > 0) {
      std::cout << "Amount of nodes with " << i << " children: " << node_counts[i] << std::endl;
    }
  }

  const auto trie = TinyTrie::BulkCreate<std::string>(testWords);
  ASSERT_EQ(trie.node_count(), std::reduce(node_counts.begin(), node_counts.end()) - node_counts[0]);
}