#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("smoke: processor constructs and reports sane defaults", "[smoke]")
{
    clearspace::ClearSpaceProcessor processor;
    CHECK(processor.getName() == "Clear Space");
    CHECK_FALSE(processor.acceptsMidi());
    CHECK_FALSE(processor.producesMidi());
    CHECK(processor.getTailLengthSeconds() == Catch::Approx(0.0));
    CHECK(processor.getBusCount(true) == 2); // main + sidechain
    CHECK(processor.getBusCount(false) == 1);
}
