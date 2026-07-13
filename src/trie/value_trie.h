#pragma once

#include <array>
#include <optional>
#include <string_view>

// TODO:
//  - Delete empty nodes
/**
 * @class ValueTrie
 * @brief A Trie implementation that stores values of a given type.
 *
 * @tparam T The value type to store.
 */
template <typename T>
class ValueTrie {
private:
  class Node {
  public:
    T value;
    bool is_terminal = false;
    std::array<Node*, 256> nodes;

    void delete_children();
  };

  Node* root;

  size_t count;

  size_t node_cnt;

public:
  ValueTrie();

  ~ValueTrie();

  size_t size() const { return count; }

  size_t node_count() const { return node_cnt; }

  bool set(std::string_view word, T value);

  bool remove(std::string_view word);

  std::optional<T> get(std::string_view word) const;

private:
  Node* create_node(std::string_view word);

  std::optional<Node*> get_node(std::string_view word);

  std::optional<const Node*> get_node(std::string_view word) const;
};

template <typename T>
void ValueTrie<T>::Node::delete_children() {
  for (auto& child : nodes) {
    if (child != nullptr) {
      child->delete_children();
      delete child;
      child = nullptr;
    }
  }
}

template <typename T>
ValueTrie<T>::ValueTrie() {
  root = new Node();
  count = 0;
  node_cnt = 1;
}

template <typename T>
ValueTrie<T>::~ValueTrie() {
  root->delete_children();
}

template <typename T>
bool ValueTrie<T>::set(const std::string_view word, T value) {
  Node* node = this->create_node(word);

  node->value = value;
  const bool new_word = !node->is_terminal;
  node->is_terminal = true;
  count += new_word;
  return new_word;
}

template <typename T>
bool ValueTrie<T>::remove(const std::string_view word) {
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
std::optional<T> ValueTrie<T>::get(const std::string_view word) const {
  const auto node = this->get_node(word);

  if (node.has_value()) {
    if (node.value()->is_terminal) {
      return node.value()->value;
    }
  }

  return {};
}

template <typename T>
ValueTrie<T>::Node* ValueTrie<T>::create_node(
    const std::string_view word) {
  Node* cur_node = root;

  for (const auto c : word) {
    if (cur_node->nodes[c] == nullptr) {
      auto node = new Node();
      node_cnt++;
      cur_node->nodes[c] = node;
      cur_node = node;
    } else {
      cur_node = cur_node->nodes[c];
    }
  }

  return cur_node;
}

template <typename T>
std::optional<typename ValueTrie<T>::Node*> ValueTrie<T>::get_node(
    std::string_view word) {
  auto node = static_cast<const ValueTrie*>(this)->get_node(word);

  if (node.has_value()) {
    return const_cast<Node*>(node.value());
  }

  return {};
}

template <typename T>
std::optional<const typename ValueTrie<T>::Node*> ValueTrie<T>::get_node(
    const std::string_view word) const {
  const Node* cur_node = root;

  for (const auto c : word) {
    if (cur_node->nodes[c] == nullptr) {
      return {};
    }

    cur_node = cur_node->nodes[c];
  }

  return cur_node;
}