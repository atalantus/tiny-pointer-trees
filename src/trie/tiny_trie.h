#pragma once

#include <algorithm>
#include <memory>
#include <numeric>
#include <string_view>

#include "tiny_ptr/deref_table.h"
#include "trie/trie_util.h"

// TODO:
//  - This Trie currently also stores the terminating null character of C-style strings
//  - Delete empty nodes
/**
 * @class TinyTrie
 * @brief A memory-efficient Trie implementation using tiny pointers and pointer tagging for leaf nodes.
 *
 * This Trie implementation uses the power-of-two-choice tiny pointer scheme to efficiently store a set
 * of keys. Each node stores 256 tiny pointers, one for each possible byte value.
 *
 * Additionally, empty leaves indicating a stored key are replaced by using a tagged tiny pointer stored
 * in the parent.
 */
class TinyTrie {
private:
  class Node {
  public:
    bool is_terminal = false;
    std::array<TinyPtr<>, 256> nodes;
  };

  using TinyPtrT = DerefTable<Node>::TinyPtrT;

  std::pair<TinyPtrT, Node*> root;

  size_t count;

  mutable DerefTable<Node> deref_table;

  explicit TinyTrie(size_t expected_number_of_nodes);

  template <typename StringT = std::string_view>
  explicit TinyTrie(const std::vector<StringT>& sorted_words);

public:
  static TinyTrie Create(const size_t expected_number_of_nodes) {
    return TinyTrie(expected_number_of_nodes);
  }

  template <typename StringT = std::string_view>
  static TinyTrie BulkCreate(const std::vector<StringT>& sorted_words) {
    return TinyTrie(sorted_words);
  }

  template <typename StringT = std::string_view>
  static TinyTrie BulkCreateUnsorted(std::vector<StringT>& words) {
    std::ranges::sort(words);
    return TinyTrie(words);
  }

  size_t size() const { return count; }

  size_t node_count() const { return deref_table.size(); }

  bool insert(std::string_view word);

  bool remove(std::string_view word);

  bool contains(std::string_view word) const;

private:
  std::pair<Node*, std::pair<uint64_t, uint64_t> > create_parent_node(
      std::string_view word) const;

  std::pair<Node*, std::pair<uint64_t, uint64_t> > get_parent_node(
      std::string_view word) const;
};

inline TinyTrie::TinyTrie(const size_t expected_number_of_nodes)
  : count(0), deref_table(DerefTable<Node>::Create(expected_number_of_nodes)) {
  root = deref_table.allocate(0, 0);
}

template <typename StringT>
TinyTrie::TinyTrie(const std::vector<StringT>& sorted_words) : count(0) {
  assert(std::ranges::is_sorted(sorted_words) &&
      "use Trie::BulkCreateUnsorted if your words are not already sorted alphabetically. Note that sorting "
      "is case-sensitive.");
  auto node_counts = compute_node_counts(sorted_words);
  const size_t node_count_sum = std::reduce(node_counts.begin(),
                                            node_counts.end());
  deref_table = DerefTable<Node>::Create(node_count_sum);

  root = deref_table.allocate(0, 0);

  for (auto word : sorted_words) { insert(std::string_view(word)); }
}