#pragma once

#include <array>
#include <cstddef>
#include <vector>
#include <string>

#include "tiny_ptr/deref_table.h"
#include "tiny_ptr/tiny_ptr.h"
#include "tiny_ptr/util.h"

class TinyAutomaton {
private:
  using ATinyPtr = TinyPtr<>;

  class Node {
  public:
    bool terminal = false;
    std::array<ATinyPtr, 256> children{};
    ATinyPtr suffixLink = ATinyPtr::null;
    ATinyPtr terminalLink = ATinyPtr::null;
    uint32_t depth;
  };

  DerefTable<Node> derefTable;

  std::pair<ATinyPtr, Node*> root;

public:
  struct Match {
    std::size_t start;
    std::size_t end;
    std::string word;
  };

  static TinyAutomaton Create(const std::vector<std::string>& words);

  [[nodiscard]] std::vector<Match> Search(const std::string& text) const;

  TinyAutomaton(const TinyAutomaton&) = delete;

  TinyAutomaton& operator=(const TinyAutomaton&) = delete;

private:
  TinyAutomaton() : root(derefTable.allocate(word_hash({}))) {
    root.second->depth = 0;
  }

  void BuildAutomatonLinks();
};