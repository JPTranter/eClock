/**
 * board_pins.h - EN05 ePaper shield pin mapping for the XIAO nRF52840
 *
 * The same physical pogo pad has a different Arduino alias depending on which
 * core is being compiled. Getting this wrong produces a display that accepts
 * SPI writes and does nothing, which is indistinguishable from a software bug.
 * See docs/hardware/PINOUT.md for the full explanation.
 *
 * Pin indices below are given as bare integers, not Dxx macros. See the comment
 * in the Adafruit branch for why that matters.
 *
 * Not used by the Phase 1 blink test; defined now so the Phase 2 display work
 * starts from a single source of truth.
 */

#ifndef ECLOCK_BOARD_PINS_H
#define ECLOCK_BOARD_PINS_H

#if defined(ECLOCK_CORE_ADAFRUIT)

  // IMPORTANT: this variant's variant.h only defines the macros D0..D10.
  // There is no `D11` or `D16` macro, so writing EPD_DC as `D16` fails to
  // compile ("'D16' was not declared in this scope"). The *pin indices* 11 and
  // 16 are nonetheless correct - g_ADigitalPinMap in variant.cpp maps index 16
  // to P0.27 and index 11 to P0.26 - so use bare numbers here.
  //
  // Verified against:
  //   framework-arduinoadafruitnrf52-seeed/variants/Seeed_XIAO_nRF52840/
  #define EPD_CS    7     // P1.12
  #define EPD_DC    16    // P0.27 - shared with the IMU's I2C SCL
  #define EPD_RST   11    // P0.26 - shared with LED_RED
  #define EPD_BUSY  3     // P0.29
  #define EPD_POWER 6     // P1.11 - MOSFET gate: HIGH or the panel is unpowered

#elif defined(ECLOCK_CORE_MBED)

  #define EPD_CS    7
  #define EPD_DC    15    // P0.27
  #define EPD_RST   11    // P0.26
  #define EPD_BUSY  3
  #define EPD_POWER 6

  /**
   * Release P0.26/P0.27 from the TWIM hardware peripheral back to plain GPIO.
   *
   * On the mbed core the hardware I2C peripheral is left enabled on these pins
   * at startup. An active peripheral overrides GPIO toggling on the nRF52, so
   * the display driver's DC writes are silently swallowed and the panel never
   * sees a command. Call this at the very top of setup(), before display init.
   *
   * Side effect: disables the onboard IMU's I2C bus.
   */
  static inline void eclock_release_twim_pins() {
      NRF_TWIM0->ENABLE = 0;
      NRF_TWIM1->ENABLE = 0;
      NRF_TWI0->ENABLE  = 0;
      NRF_TWI1->ENABLE  = 0;
      NRF_GPIO->PIN_CNF[26] = 3;  // RST -> standard GPIO
      NRF_GPIO->PIN_CNF[27] = 3;  // DC  -> standard GPIO
  }

#else
  #error "Define ECLOCK_CORE_ADAFRUIT or ECLOCK_CORE_MBED (see platformio.ini)"
#endif

// Panel: GDEY029T94 / FPC-A005, 296x128 mono, SSD1680 controller.
// GxEPD2 driver class: GxEPD2_290_T94_V2
#define EPD_WIDTH  296
#define EPD_HEIGHT 128

#endif  // ECLOCK_BOARD_PINS_H
