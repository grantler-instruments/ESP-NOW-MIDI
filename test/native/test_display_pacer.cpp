// Dongle display redraws wait for a pause in MIDI, so a slow redraw does not
// hold up bridging while notes flow.

#include <catch2/catch_test_macros.hpp>

#include "include/DisplayPacer.h"

using enomik::DisplayPacer;

namespace {

DisplayPacer pacer()
{
    DisplayPacer p; // 200 ms interval, 100 ms quiet, 1000 ms max defer
    p.drawn(0);
    return p;
}

} // namespace

TEST_CASE("the first redraw is right away", "[display]")
{
    DisplayPacer p;
    REQUIRE(p.due(0));
}

TEST_CASE("without MIDI the display redraws every interval", "[display]")
{
    DisplayPacer p = pacer();
    REQUIRE_FALSE(p.due(199));
    REQUIRE(p.due(200));
}

TEST_CASE("redraws wait until MIDI was quiet", "[display]")
{
    DisplayPacer p = pacer();
    p.noteMidi(250);
    REQUIRE_FALSE(p.due(300));
    REQUIRE_FALSE(p.due(349));
    REQUIRE(p.due(350));
}

TEST_CASE("continuous MIDI still gets a redraw after the max defer time", "[display]")
{
    DisplayPacer p = pacer();
    for (uint32_t t = 0; t < 1000; t += 10)
    {
        p.noteMidi(t);
        REQUIRE_FALSE(p.due(t));
    }
    p.noteMidi(1000);
    REQUIRE(p.due(1000));
    p.drawn(1000);
    p.noteMidi(1490);
    REQUIRE_FALSE(p.due(1500)); // next forced redraw at 2000
    REQUIRE(p.due(2000));
}

TEST_CASE("UI input redraws at once, even while MIDI flows", "[display]")
{
    DisplayPacer p = pacer();
    p.noteMidi(50);
    p.requestNow();
    REQUIRE(p.due(60));
    p.drawn(60);
    REQUIRE_FALSE(p.due(70));
}

TEST_CASE("quiet time 0 ignores MIDI, max defer 0 waits for quiet", "[display]")
{
    DisplayPacer p = pacer();
    p.quietMs = 0;
    p.noteMidi(200);
    REQUIRE(p.due(200));

    p = pacer();
    p.maxDeferMs = 0;
    for (uint32_t t = 0; t < 5000; t += 10)
    {
        p.noteMidi(t);
        REQUIRE_FALSE(p.due(t));
    }
    REQUIRE(p.due(5090));
}

TEST_CASE("pacing survives millis() wrapping", "[display]")
{
    DisplayPacer p;
    p.drawn(0xFFFFFF00u);
    p.noteMidi(0xFFFFFFF0u);
    REQUIRE_FALSE(p.due(0x00000010u)); // 32 ms after MIDI
    REQUIRE(p.due(0x00000060u));       // 112 ms after MIDI, 352 ms after the redraw
}
