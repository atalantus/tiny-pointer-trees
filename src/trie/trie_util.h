#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <string_view>
#include <vector>
#include <cstdint>

inline uint64_t djb2_hash_init = 5381;

inline uint64_t sdbm_hash_init = 0;

inline uint64_t djb2_hash(const char c, const uint64_t h) {
    return (h << 5) + h + c;
}

inline uint64_t sdbm_hash(const char c, const uint64_t h) {
    return (h << 6) + (h << 16) - h + c;
}

template<typename StringT = std::string_view>
std::array<size_t, 257> compute_node_counts_for_depth(const std::vector<StringT> &sorted_words, const size_t start,
                                                      const size_t end, const size_t depth) {
    auto res = std::array<size_t, 257>{};
    res.fill(0);

    if (start >= end) { return res; }

    // the actual start index of the first word
    size_t first_start = end;

    for (auto i = start; i < end; ++i) {
        if (depth < sorted_words[i].size()) {
            first_start = i;
            break;
        }
    }

    if (first_start == end) {
        // leaf node
        res[0] = 1;
        return res;
    }

    // the actual end index of the last word
    size_t last_end = end - 1;
    for (; last_end > first_start; --last_end) {
        if (depth < sorted_words[last_end].size()) { break; }
    }

    size_t cur_node_child_count = 1;
    size_t cur_char_start = first_start;
    auto cur_char = sorted_words[first_start][depth];

    if (last_end > first_start && sorted_words[last_end][depth] != cur_char) {
        // node has at least two different children -> iterate over all words in range
        for (auto i = first_start + 1; i <= last_end; ++i) {
            if (depth >= sorted_words[i].size()) { continue; }

            if (sorted_words[i][depth] != cur_char) {
                cur_node_child_count++;

                // compute node counts for subtree
                auto subtree_node_count = compute_node_counts_for_depth(sorted_words, cur_char_start, i, depth + 1);
                for (size_t j = 0; j < 257; ++j) { res[j] += subtree_node_count[j]; }

                cur_char_start = i;
                cur_char = sorted_words[i][depth];
            }
        }
    }

    // handle last character subgroup
    const auto subtree_node_count = compute_node_counts_for_depth(sorted_words, cur_char_start, end, depth + 1);
    for (size_t j = 0; j < 257; ++j) { res[j] += subtree_node_count[j]; }

    // count current node
    assert(cur_node_child_count < 257);
    res[cur_node_child_count]++;

    return res;
}

/**
 * Counts how many nodes of a specific fill count we would get when constructing a trie over a range of words.
 * Note that the i-th element represents the number of nodes containing i elements/children
 *
 * @param sorted_words - the words sorted in alphabetical order
 * @return - an array where the i-th element is the number of nodes containing i elements/children
 */
// TODO: Optimize? Better algorithm, remove recursion, vectorization (make array size 256 emitting leaf nodes?),
//  parallelization, etc.?
template<typename StringT = std::string_view>
std::array<size_t, 257> compute_node_counts(const std::vector<StringT> &sorted_words) {
    assert(std::is_sorted(sorted_words.begin(), sorted_words.end()) && "words must be sorted in ascending order");

    return compute_node_counts_for_depth(sorted_words, 0, sorted_words.size(), 0);
}
