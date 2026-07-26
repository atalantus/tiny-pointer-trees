#include "tiny_automaton.hpp"

#include <queue>

#include "tiny_ptr/util.h"

void BuildAutomatonLinks()

TinyAutomaton TinyAutomaton::Create(const std::vector<std::string>& words) {
  TinyAutomaton automaton;

  // Step 1: Build Trie
  for (auto& word : words) {
    auto node = automaton.root;

    std::vector<char> prefix;

    for (auto& c : word) {
      prefix.push_back(c);
      auto i = static_cast<unsigned char>(c);

      if (node.second->children[i] == ATinyPtr::null) {
        const auto newNode = automaton.derefTable.allocate(word_hash(prefix));
        newNode.second->depth = node.second->depth + 1;
        node.second->children[i] = newNode.first;
        node = newNode;
      } else {
        node = {node.second->children[i],
                automaton.derefTable.dereference(
                    node.second->children[i], word_hash(prefix))};
      }
    }

    node.second->terminal = true;
  }

  // Step 2: Build Links using BFS based construction approach
  automaton.BuildAutomatonLinks();

  return automaton;
}

std::vector<TinyAutomaton::Match> TinyAutomaton::Search(
    const std::string& text) const {
  std::vector<Match> matches;
  auto node = root;

  std::queue<char> prefix;

  for (std::size_t i = 0; i < text.size(); ++i) {
    auto c = static_cast<unsigned char>(text[i]);

    // Follow suffix links until we can consume c or fall back to the root
    while (node != root && node.second->children[c] == ATinyPtr::null) {
      node = node->suffixLink;
    }

    if (node.second->children[c] != ATinyPtr::null) {
      node = node->children[c];
    }

    // Report the word ending here and every shorter word that is a suffix of it
    for (auto match = node; match != nullptr; match = match->terminalLink) {
      if (match->isTerminal()) {
        auto start = i + 1 - match->wordLength;
        matches.push_back({start, i, text.substr(start, match->wordLength)});
      }
    }
  }

  return matches;
}

void TinyAutomaton::BuildAutomatonLinks() {
  std::queue<std::tuple<ATinyPtr, Node*, std::deque<char> > > queue;

  // The root's direct children fall back to the root on mismatch
  for (int i = 0; i < 256; ++i) {
    auto childTinyPtr = root.second->children[i];

    if (childTinyPtr != ATinyPtr::null) {
      auto childNode = derefTable.dereference(
          childTinyPtr, word_hash({static_cast<char>(i)}));

      childNode->suffixLink = root.first;
      queue.emplace(childTinyPtr, childNode,
                    std::deque{static_cast<char>(i)});
    }
  }

  while (!queue.empty()) {
    auto node = queue.front();
    queue.pop();

    // The terminal link points to the nearest suffix that ends a word
    std::get<1>(node)->terminalLink = node->suffixLink->isTerminal()
                                        ? node->suffixLink
                                        : node->suffixLink->terminalLink;

    for (int c = 0; c < 256; ++c) {
      auto child = node->children[c];

      if (child == nullptr) {
        continue;
      }

      // Follow suffix links until a node that has a 'c' edge, or the root
      auto fallback = node->suffixLink;

      while (fallback != root && fallback->children[c] == nullptr) {
        fallback = fallback->suffixLink;
      }

      child->suffixLink = fallback->children[c] != nullptr && fallback->children
                          [c] != child
                            ? fallback->children[c]
                            : root;

      queue.push(child);
    }
  }
}