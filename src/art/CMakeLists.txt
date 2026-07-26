add_library(art STATIC
        ARTSynchronized/OptimisticLockCoupling/Tree.cpp
        tiny_art/Tree.cpp
        tiny_art_64/Tree.cpp
        tiny_art_256/Tree.cpp
)

target_include_directories(art PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

find_library(TbbLib tbb REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(art PUBLIC ${TbbLib} Threads::Threads)

add_executable(benchmark benchmark.cpp)
target_link_libraries(benchmark PUBLIC art)

add_executable(benchmark_perfevent benchmark_perfevent.cpp)
target_link_libraries(benchmark_perfevent PUBLIC art)