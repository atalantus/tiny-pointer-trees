add_library(art STATIC
        ARTSynchronized/OptimisticLockCoupling/Tree.cpp
        tiny_art/Tree.cpp
)

target_include_directories(art PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

find_library(TbbLib tbb REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(art PUBLIC ${TbbLib} Threads::Threads)
