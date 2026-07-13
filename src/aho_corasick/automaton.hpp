#pragma once

#include <array>
#include <cstddef>
#include <vector>
#include <string>

class Automaton {
private:
  class Node {
  public:
    std::array<Node*, 256> children{};
    Node* suffixLink = nullptr;
    Node* terminalLink = nullptr;
    std::size_t wordLength = -1;

    [[nodiscard]] bool isTerminal() const { return wordLength != -1; }

    void deleteChildren() const;
  };

  Node* root;

public:
  struct Match {
    std::size_t start;
    std::size_t end;
    std::string word;
  };

  ~Automaton() {
    if (root != nullptr) {
      root->deleteChildren();
      delete root;
    }
  }

  static Automaton Create(const std::vector<std::string>& words);

  [[nodiscard]] std::vector<Match> Search(const std::string& text) const;

  Automaton(const Automaton&) = delete;

  Automaton& operator=(const Automaton&) = delete;

  Automaton(Automaton&& other) noexcept : root(other.root) {
    other.root = nullptr;
  }

  Automaton& operator=(Automaton&& other) noexcept {
    if (this != &other) {
      if (root != nullptr) {
        root->deleteChildren();
        delete root;
      }
      root = other.root;
      other.root = nullptr;
    }
    return *this;
  }

private:
  Automaton() : root(new Node()) {
  }
};