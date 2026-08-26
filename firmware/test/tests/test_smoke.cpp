// test_smoke.cpp — proves the whole fixture compiles, links, and that the
// firmware's static state/functions are reachable through clock_harness.h.

#include "clock_harness.h"
#include <gtest/gtest.h>

// 1. The firmware compiles against the mocks (this file includes main.cpp).
// 2. A trivial reset + draw + PNG dump round-trips.

TEST(Smoke, FirmwareCompilesAndLinks) {
    ClockFixture::reset();
    // Static state is reachable (proves #include "main.cpp" works).
    EXPECT_EQ(ClockFixture::state(), STATE_SYNCING);
    EXPECT_EQ(ClockFixture::epoch(), 0);
}

TEST(Smoke, DrawAndDumpPng) {
    ClockFixture::reset();
    // Put the clock in a drawable state and render once.
    ClockFixture::setEpoch(1693000000, 36000);   // a fixed Tuesday
    g_state = STATE_RUNNING;
    ClockFixture::runDrawClockFace();
    EXPECT_TRUE(ClockFixture::dumpPng("output/smoke.png"));
}
