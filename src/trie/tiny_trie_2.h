#pragma once

#include <algorithm>
#include <memory>
#include <numeric>
#include <string_view>
#include <gtest/gtest_prod.h>

#include "tiny_ptr/deref_table.h"
#include "trie/trie_util.h"

// TODO:
//  - Delete empty nodes
/**
 * @class TinyTrie2
 * @brief A memory-efficient Trie implementation using tiny pointers and pointer tagging for leaf nodes.
 *
 * This Trie implementation uses the power-of-two-choice tiny pointer scheme to efficiently store a set
 * of keys. Each node stores 256 tiny pointers, one for each possible byte value.
 *
 * Additionally, empty leaves indicating a stored key are replaced by using a tagged tiny pointer stored
 * in the parent.
 *
 * As stable ID the memory location of the parent node together with the character of the edge from the
 * parent to the node is used.
 */
class TinyTrie2 {
  FRIEND_TEST(TestTrie, TinyTrie2RemoveDeletesEmptyNodes);

private:
  class Node {
  public:
    bool is_terminal = false;
    std::array<TinyPtr<>, 256> nodes;
  };

  static_assert(sizeof(Node) >= 256,
                "Node must be at least 1 byte in size to store free slot index")
  ;

  using TinyPtrT = DerefTable<Node>::TinyPtrT;

  std::pair<TinyPtrT, Node*> root;

  size_t count;

  mutable DerefTable<Node> deref_table;

  explicit TinyTrie2(size_t expected_number_of_nodes);

  template <typename StringT = std::string_view>
  explicit TinyTrie2(const std::vector<StringT>& sorted_words);

public:
  static TinyTrie2 Create(const size_t expected_number_of_nodes) {
    return TinyTrie2(expected_number_of_nodes);
  }

  template <typename StringT = std::string_view>
  static TinyTrie2 BulkCreate(const std::vector<StringT>& sorted_words) {
    return TinyTrie2(sorted_words);
  }

  template <typename StringT = std::string_view>
  static TinyTrie2 BulkCreateUnsorted(std::vector<StringT>& words) {
    std::ranges::sort(words);
    return TinyTrie2(words);
  }

  size_t size() const { return count; }

  size_t node_count() const { return deref_table.size(); }

  bool insert(std::string_view word);

  bool remove(std::string_view word);

  bool contains(std::string_view word) const;

private:
  Node* create_parent_node(std::string_view word) const;

  Node* get_parent_node(std::string_view word) const;

  static size_t compute_child_count(const Node* node);
};

inline TinyTrie2::TinyTrie2(const size_t expected_number_of_nodes)
  : count(0), deref_table(DerefTable<Node>::Create(expected_number_of_nodes)) {
  root = deref_table.allocate({0, 0});
}

template <typename StringT>
TinyTrie2::TinyTrie2(const std::vector<StringT>& sorted_words) : count(0) {
  assert(std::ranges::is_sorted(sorted_words) &&
      "use BulkCreateUnsorted if your words are not already sorted alphabetically. Note that sorting "
      "is case-sensitive.");
  auto node_counts = compute_node_counts(sorted_words);
  const size_t node_count_sum = std::reduce(node_counts.begin(),
                                            node_counts.end());
  deref_table = DerefTable<Node>::Create(node_count_sum);

  root = deref_table.allocate({0, 0});

  for (auto word : sorted_words) { insert(std::string_view(word)); }
}