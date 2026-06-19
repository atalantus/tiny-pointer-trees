#include "trie/tiny_trie_2.h"
#include "trie/trie_util.h"

bool TinyTrie2::insert(const std::string_view word) {
  Node* cur_node = root.second;

  if (!word.empty()) {
    auto parent_node = this->create_parent_node(word);
    assert(parent_node);

    cur_node = parent_node;

    // handle the last character (if word is not empty)
    const auto c = word.back();

    if (cur_node->nodes[c] == TinyPtrT::tagged) {
      // word already exists
      return false;
    }

    if (cur_node->nodes[c] == TinyPtrT::null) {
      // use tagged pointer to avoid creating empty leaf node
      cur_node->nodes[c] = TinyPtrT::tagged;
      count++;
      return true;
    }

    // node already exists, go to it
    cur_node = this->deref_table.dereference(cur_node->nodes[c],
                                             address_hash(cur_node, c));
  }

  // mark node as terminal and update count
  const bool new_word = !cur_node->is_terminal;
  cur_node->is_terminal = true;
  count += new_word;
  return new_word;
}

bool TinyTrie2::remove(const std::string_view word) {
  Node* cur_node = root.second;

  if (!word.empty()) {
    auto parent_node = this->get_parent_node(word);

    if (!parent_node) {
      return false;
    }

    cur_node = parent_node;

    // handle the last character (if word is not empty)
    const auto c = word.back();

    if (cur_node->nodes[c] == TinyPtrT::null) {
      return false;
    }

    if (cur_node->nodes[c] == TinyPtrT::tagged) {
      cur_node->nodes[c] = TinyPtrT::null;
      count--;
      return true;
    }

    // node exists, go to it
    cur_node = this->deref_table.dereference(cur_node->nodes[c],
                                             address_hash(cur_node, c));
  }

  // mark node as terminal and update count
  const auto is_terminal = cur_node->is_terminal;
  cur_node->is_terminal = false;
  count--;
  return is_terminal;
}

bool TinyTrie2::contains(const std::string_view word) const {
  const Node* cur_node = root.second;

  if (!word.empty()) {
    auto parent_node = this->get_parent_node(word);

    if (!parent_node) {
      return false;
    }

    cur_node = parent_node;

    // handle the last character (if word is not empty)
    const auto c = word.back();

    if (cur_node->nodes[c] == TinyPtrT::null) {
      return false;
    }

    if (cur_node->nodes[c] == TinyPtrT::tagged) {
      return true;
    }

    // node exists, go to it
    cur_node = this->deref_table.dereference(cur_node->nodes[c],
                                             address_hash(cur_node, c));
  }

  // mark node as terminal and update count
  return cur_node->is_terminal;
}

TinyTrie2::Node*
TinyTrie2::create_parent_node(std::string_view word) const {
  Node* cur_node = root.second;

  // traverse all characters except the last
  for (size_t i = 0; i < word.size() - 1; ++i) {
    const auto c = word[i];
    const auto h = address_hash(cur_node, c);

    const auto is_tagged = cur_node->nodes[c] == TinyPtrT::tagged;

    if (cur_node->nodes[c] == TinyPtrT::null || is_tagged) {
      auto [ptr, node] = this->deref_table.allocate(h);
      cur_node->nodes[c] = ptr;
      cur_node = node;
      cur_node->is_terminal = is_tagged;
    } else {
      cur_node = this->deref_table.dereference(cur_node->nodes[c], h);
    }
  }

  return cur_node;
}

TinyTrie2::Node*
TinyTrie2::get_parent_node(std::string_view word) const {
  Node* cur_node = root.second;

  // traverse all characters except the last
  for (size_t i = 0; i < word.size() - 1; ++i) {
    const auto c = word[i];

    if (cur_node->nodes[c] == TinyPtrT::null || cur_node->nodes[c] ==
        TinyPtrT::tagged) {
      return nullptr;
    }

    cur_node = this->deref_table.dereference(cur_node->nodes[c],
                                             address_hash(cur_node, c));
  }

  return cur_node;
}