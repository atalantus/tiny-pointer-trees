#include "trie/tiny_trie.h"
#include "trie/trie_util.h"

bool TinyTrie::insert(const std::string_view word) {
  Node *cur_node = root.second;

  if (!word.empty()) {
    auto [parent_node, h] = this->create_parent_node(word);
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
    const auto h1 = djb2_hash(c, h.first);
    const auto h2 = sdbm_hash(c, h.second);
    cur_node      = this->deref_table.dereference(cur_node->nodes[c], h1, h2);
  }

  // mark node as terminal and update count
  const bool new_word   = !cur_node->is_terminal;
  cur_node->is_terminal = true;
  count += new_word;
  return new_word;
}

bool TinyTrie::remove(const std::string_view word) {
  Node *cur_node = root.second;

  if (!word.empty()) {
    auto [parent_node, h] = this->get_parent_node(word);

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
    const auto h1 = djb2_hash(c, h.first);
    const auto h2 = sdbm_hash(c, h.second);
    cur_node      = this->deref_table.dereference(cur_node->nodes[c], h1, h2);
  }

  // mark node as terminal and update count
  const auto is_terminal = cur_node->is_terminal;
  cur_node->is_terminal  = false;
  count--;
  return is_terminal;
}

bool TinyTrie::contains(const std::string_view word) const {
  const Node *cur_node = root.second;

  if (!word.empty()) {
    auto [parent_node, h] = this->get_parent_node(word);

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
    const auto h1 = djb2_hash(c, h.first);
    const auto h2 = sdbm_hash(c, h.second);
    cur_node      = this->deref_table.dereference(cur_node->nodes[c], h1, h2);
  }

  // mark node as terminal and update count
  return cur_node->is_terminal;
}

std::pair<TinyTrie::Node *, std::pair<uint64_t, uint64_t>> TinyTrie::create_parent_node(std::string_view word) const {
  Node *cur_node = root.second;
  uint64_t h1    = 5381;
  uint64_t h2    = 0;

  // traverse all characters except the last
  for (size_t i = 0; i < word.size() - 1; ++i) {
    const auto c = word[i];
    h1           = djb2_hash(c, h1);
    h2           = sdbm_hash(c, h2);

    const auto is_tagged = cur_node->nodes[c] == TinyPtrT::tagged;

    if (cur_node->nodes[c] == TinyPtrT::null || is_tagged) {
      auto [ptr, node]      = this->deref_table.allocate(h1, h2);
      cur_node->nodes[c]    = ptr;
      cur_node              = node;
      cur_node->is_terminal = is_tagged;
    } else {
      cur_node = this->deref_table.dereference(cur_node->nodes[c], h1, h2);
    }
  }

  return {cur_node, {h1, h2}};
}

std::pair<TinyTrie::Node *, std::pair<uint64_t, uint64_t>> TinyTrie::get_parent_node(std::string_view word) const {
  Node *cur_node = root.second;
  uint64_t h1    = 5381;
  uint64_t h2    = 0;

  // traverse all characters except the last
  for (size_t i = 0; i < word.size() - 1; ++i) {
    const auto c = word[i];
    h1           = djb2_hash(c, h1);
    h2           = sdbm_hash(c, h2);

    if (cur_node->nodes[c] == TinyPtrT::null || cur_node->nodes[c] == TinyPtrT::tagged) {
      return {nullptr, {h1, h2}};
    }

    cur_node = this->deref_table.dereference(cur_node->nodes[c], h1, h2);
  }

  return {cur_node, {h1, h2}};
}