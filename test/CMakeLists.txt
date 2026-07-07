add_executable(tiny_pointer_trees_tests
        test_art.cpp
        test_tinyptr.cpp
        test_trie.cpp
)

target_include_directories(tiny_pointer_trees_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(tiny_pointer_trees_tests PRIVATE
        tiny_pointer_trees
        art
        GTest::gtest_main
)

find_package(Threads REQUIRED)
target_link_libraries(tiny_pointer_trees_tests PRIVATE Threads::Threads)

gtest_discover_tests(tiny_pointer_trees_tests)
