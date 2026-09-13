#include "PluginProcessor.h"
#include "dsp/Routing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("smoke: processor constructs and reports sane defaults", "[smoke]")
{
    clearspace::ClearSpaceProcessor processor;
    CHECK(processor.getName() == "Clear Space");
    CHECK_FALSE(processor.acceptsMidi());
    CHECK_FALSE(processor.producesMidi());
    // Defaults: delay 375 ms at 35 % feedback, reverb decay 2 s → tail is the longer of the two.
    CHECK(processor.getTailLengthSeconds() ==
          Catch::Approx(clearspace::dsp::Routing::tailSeconds(375.0, 35.0, false, 2.0, false)));
    CHECK(processor.getTailLengthSeconds() > 2.0);
    CHECK(processor.getLatencySamples() == 0);
    CHECK(processor.getBusCount(true) == 2); // main + sidechain
    CHECK(processor.getBusCount(false) == 1);
}
