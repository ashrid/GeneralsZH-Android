// GeneralsX @feature Claude 10/07/2026 Task 8: sanity test — verifies the doctest + ctest
// infrastructure works (doctest links, ctest discovers + runs the test). The 7 adversarial
// ArchiveFileSystem multimap-dance tests are added next (need engine init + synthetic .big).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

TEST_CASE("sanity: doctest + ctest infrastructure works")
{
	REQUIRE(1 == 1);
	REQUIRE(true);
	CHECK(2 + 2 == 4);
}
