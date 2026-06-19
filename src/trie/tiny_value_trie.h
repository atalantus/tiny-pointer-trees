#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "tiny_ptr/deref_table.h"
#include "trie/trie_util.h"

// TODO:
//  - Delete empty nodes
/**
 * @class TinyValueTrie
 * @brief A memory-efficient Trie implementation using tiny pointers that stores values of a given type.
 *
 * @tparam T The value type to store.
 */
template <typename T>
class TinyValueTrie {
private:
  class Node {
  public:
    T value;
    bool is_terminal = false;
    std::array<TinyPtr<>, 256> nodes;
  };

  using TinyPtrT = DerefTable<Node>::TinyPtrT;

  std::pair<TinyPtrT, Node*> root;

  size_t count;

  mutable DerefTable<Node> deref_table{};

public:
  explicit TinyValueTrie(size_t expected_number_of_nodes);

  size_t size() const { return count; }

  size_t node_count() const { return deref_table.size(); }

  bool set(std::string_view word, T value);

  bool remove(std::string_view word);

  std::optional<T> get(std::string_view word) const;

private:
  Node* create_node(std::string_view word);

  std::optional<Node*> get_node(std::string_view word);

  std::optional<const Node*> get_node(std::string_view word) const;
};

template <typename T>
TinyValueTrie<T>::TinyValueTrie(const size_t expected_number_of_nodes)
  : deref_table(DerefTable<Node>::Create(expected_number_of_nodes)) {
  count = 0;
  root = deref_table.allocate({0, 0});
}

template <typename T>
bool TinyValueTrie<T>::set(const std::string_view word, T value) {
  Node* node = this->create_node(word);

  node->value = value;
  const bool new_word = !node->is_terminal;
  node->is_terminal = true;
  count += new_word;
  return new_word;
}

template <typename T>
bool TinyValueTrie<T>::remove(const std::string_view word) {
  const auto node = this->get_node(word);

  if (node.has_value()) {
    if (node.value()->is_terminal) {
      node.value()->is_terminal = false;
      count--;
      return true;
    }
  }

  return false;
}

template <typename T>
std::optional<T> TinyValueTrie<T>::get(const std::string_view word) const {
  const auto node = this->get_node(word);

  if (node.has_value()) {
    if (node.value()->is_terminal) {
      return node.value()->value;
    }
  }

  return {};
}

template <typename T>
TinyValueTrie<T>::Node* TinyValueTrie<T>::create_node(
    const std::string_view word) {
  Node* cur_node = root.second;
  auto h = std::make_pair(djb2_hash_init, sdbm_hash_init);

  for (const auto c : word) {
    h = djb2_sdbm_hash(c, h);

    if (cur_node->nodes[c] == TinyPtrT::null) {
      auto [ptr, node] = this->deref_table.allocate(h);
      cur_node->nodes[c] = ptr;
      cur_node = node;
    } else {
      cur_node = this->deref_table.dereference(cur_node->nodes[c], h);
    }
  }

  return cur_node;
}

template <typename T>
std::optional<typename TinyValueTrie<T>::Node*> TinyValueTrie<T>::get_node(
    std::string_view word) {
  auto node = static_cast<const TinyValueTrie*>(this)->get_node(word);

  if (node.has_value()) {
    return const_cast<Node*>(node.value());
  }

  return {};
}

template <typename T>
std::optional<const typename TinyValueTrie<T>::Node*> TinyValueTrie<
  T>::get_node(const std::string_view word) const {
  const Node* cur_node = root.second;
  auto h = std::make_pair(djb2_hash_init, sdbm_hash_init);

  for (const auto c : word) {
    h = djb2_sdbm_hash(c, h);

    if (cur_node->nodes[c] == TinyPtrT::null) {
      return {};
    }

    cur_node = this->deref_table.dereference(cur_node->nodes[c], h);
  }

  return cur_node;
}