#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>

TEST_CASE("the CPU test process uses the locked Windows x64 ABI", "[bootstrap]")
{
    STATIC_REQUIRE(sizeof(void *) == 8);
    REQUIRE(std::endian::native == std::endian::little);
    REQUIRE(sizeof(std::uint64_t) == 8);
}
