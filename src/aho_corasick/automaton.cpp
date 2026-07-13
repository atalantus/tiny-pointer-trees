#include "automaton.hpp"

#include <queue>

void Automaton::Node::deleteChildren() const {
  for (auto& c : children) {
    if (c != nullptr) {
      c->deleteChildren();
      delete c;
    }
  }
}

Automaton Automaton::Create(const std::vector<std::string>& words) {
  Automaton automaton;

  // Step 1: Build Trie
  for (auto& word : words) {
    auto node = automaton.root;

    for (auto& c : word) {
      auto i = static_cast<unsigned char>(c);

      if (node->children[i] == nullptr) {
        const auto newNode = new Node();
        node->children[i] = newNode;
      }

      node = node->children[i];
    }

    node->wordLength = word.size();
  }

  // Step 2: Build Links using BFS based construction approach
  std::queue<Node*> queue;

  // The root's direct children fall back to the root on mismatch.
  for (auto child : automaton.root->children) {
    if (child != nullptr) {
      child->suffixLink = automaton.root;
      queue.push(child);
    }
  }

  while (!queue.empty()) {
    auto node = queue.front();
    queue.pop();

    // The terminal link points to the nearest suffix that ends a word.
    node->terminalLink = node->suffixLink->isTerminal()
                           ? node->suffixLink
                           : node->suffixLink->terminalLink;

    for (int c = 0; c < 256; ++c) {
      auto child = node->children[c];

      if (child == nullptr) {
        continue;
      }

      // Follow suffix links until a node that has a 'c' edge, or the root.
      auto fallback = node->suffixLink;

      while (fallback != automaton.root && fallback->children[c] == nullptr) {
        fallback = fallback->suffixLink;
      }

      child->suffixLink = fallback->children[c] != nullptr && fallback->children
                          [c] != child
                            ? fallback->children[c]
                            : automaton.root;

      queue.push(child);
    }
  }

  return automaton;
}

std::vector<Automaton::Match> Automaton::Search(const std::string& text) const {
  std::vector<Match> matches;
  auto node = root;

  for (std::size_t i = 0; i < text.size(); ++i) {
    auto c = static_cast<unsigned char>(text[i]);

    // Follow suffix links until we can consume c or fall back to the root.
    while (node != root && node->children[c] == nullptr) {
      node = node->suffixLink;
    }

    if (node->children[c] != nullptr) {
      node = node->children[c];
    }

    // Report the word ending here and every shorter word that is a suffix of it.
    for (auto match = node; match != nullptr; match = match->terminalLink) {
      if (match->isTerminal()) {
        auto start = i + 1 - match->wordLength;
        matches.push_back({start, i, text.substr(start, match->wordLength)});
      }
    }
  }

  return matches;
}