add_library(aho_corasick STATIC
        automaton.cpp
)

target_include_directories(aho_corasick PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
