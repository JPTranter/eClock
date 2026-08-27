// test_battery.cpp — getBatteryPercent(): USB branch, divider sequencing,
// and the voltage->percentage clamp.

#include "clock_harness.h"
#include <gtest/gtest.h>

TEST(Battery, UsbConnectedReturnsMinusOne) {
    ClockFixture::reset();
    nrf_power_instance.USBREGSTATUS = POWER_USBREGSTATUS_VBUSDETECT_Msk;
    EXPECT_EQ(ClockFixture::runGetBatteryPercent(), -1);
}

TEST(Battery, DividerEnabledAndDisabledAroundRead) {
    ClockFixture::reset();
    nrf_power_instance.USBREGSTATUS = 0;      // battery mode
    ClockFixture::runGetBatteryPercent();

    // Find the divider enable/disable writes to pin 14.
    bool saw_enable = false, saw_low = false, saw_high = false;
    for (int i = 0; i < test_pin_log_size(); i++) {
        const PinEvent* e = test_pin_log(i);
        if (e->op == PinOp::PIN_MODE && e->pin == 14 && e->value == OUTPUT)
            saw_enable = true;
        if (e->op == PinOp::DIGITAL_WRITE && e->pin == 14 && e->value == LOW)
            saw_low = true;
        if (e->op == PinOp::DIGITAL_WRITE && e->pin == 14 && e->value == HIGH)
            saw_high = true;
    }
    EXPECT_TRUE(saw_enable);
    EXPECT_TRUE(saw_low);
    EXPECT_TRUE(saw_high);
}

TEST(Battery, ZeroVoltageClampsToZero) {
    ClockFixture::reset();
    nrf_power_instance.USBREGSTATUS = 0;
    // raw 12-bit = 0 -> vbat = 0 -> pct clamps to 0.
    test_set_analog_u16(0);
    EXPECT_EQ(ClockFixture::runGetBatteryPercent(), 0);
}

TEST(Battery, FullScaleClampsToOneHundred) {
    ClockFixture::reset();
    nrf_power_instance.USBREGSTATUS = 0;
    // read_u16() full scale 0xFFFF >> 4 = 4095 -> vbat ~7.2V -> pct clamps 100.
    test_set_analog_u16(0xFFFF);
    EXPECT_EQ(ClockFixture::runGetBatteryPercent(), 100);
}

TEST(Battery, MidVoltageYieldsPositivePercent) {
    ClockFixture::reset();
    nrf_power_instance.USBREGSTATUS = 0;
    // 12-bit raw = 2133 -> vbat ~3.75V -> pct ~50.
    test_set_analog_u16(2133 << 4);
    int pct = ClockFixture::runGetBatteryPercent();
    EXPECT_GT(pct, 0);
    EXPECT_LT(pct, 100);
    EXPECT_NEAR(pct, 50, 2);
}

// At or below the critical threshold, the loop draws the low-battery screen and
// sets the lock so the normal clock face is never redrawn over it.
TEST(Battery, CriticallyLowSetsLockAndDrawsLowBatteryScreen) {
    ClockFixture::reset();
    nrf_power_instance.USBREGSTATUS = 0;
    ClockFixture::setEpoch(1693000000, 36000);
    g_state = STATE_RUNNING;

    // Critical: 5% -> vbat ~3.345V.
    test_set_analog_u16((uint32_t)((3.345f / 7.2f) * 4096.0f) << 4);
    ASSERT_LE(ClockFixture::runGetBatteryPercent(), CRITICAL_BATTERY_PCT);

    ClockFixture::runLoop();
    EXPECT_TRUE(ClockFixture::lowBatteryLock());
    EXPECT_FALSE(g_needs_display_update);
}

// Above the critical threshold, a normal run does NOT engage the lock.
TEST(Battery, AboveCriticalDoesNotLock) {
    ClockFixture::reset();
    nrf_power_instance.USBREGSTATUS = 0;
    ClockFixture::setEpoch(1693000000, 36000);
    g_state = STATE_RUNNING;
    // 50%: vbat ~3.75V.
    test_set_analog_u16(2133 << 4);
    ASSERT_GT(ClockFixture::runGetBatteryPercent(), CRITICAL_BATTERY_PCT);
    ClockFixture::runLoop();
    EXPECT_FALSE(ClockFixture::lowBatteryLock());
}

// REGRESSION: on USB power getBatteryPercent() returns -1 (not a percentage).
// The loop must NOT treat that as critically low and draw the LOW BATTERY
// screen, or a USB-powered clock shows a false low-battery warning on boot.
TEST(Battery, UsbPowerDoesNotTriggerLowBattery) {
    ClockFixture::reset();
    nrf_power_instance.USBREGSTATUS = POWER_USBREGSTATUS_VBUSDETECT_Msk;
    ClockFixture::setEpoch(1693000000, 36000);
    g_state = STATE_RUNNING;

    EXPECT_EQ(ClockFixture::runGetBatteryPercent(), -1);

    ClockFixture::runLoop();
    EXPECT_FALSE(ClockFixture::lowBatteryLock());
}
