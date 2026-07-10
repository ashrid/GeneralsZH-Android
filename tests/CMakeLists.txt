# GeneralsX @feature Claude 10/07/2026 Task 8 (D1): adversarial test harness (doctest).
# Foundation: doctest + ctest infrastructure. The 7 adversarial ArchiveFileSystem
# multimap-dance tests (engine init + synthetic .big + assertions) are added next.
find_package(doctest CONFIG REQUIRED)

# Sanity test — verifies doctest links + ctest discovers/runs the test (no engine deps).
add_executable(sanity_test sanity_test.cpp)
target_link_libraries(sanity_test PRIVATE doctest::doctest)
add_test(NAME sanity_test COMMAND sanity_test)
