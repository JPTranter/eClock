/**
 * board_pins.h - EN05 ePaper shield pin mapping for the XIAO nRF52840
 *
 * IMPORTANT: there are two incompatible generations of this hardware.
 *
 *   Generation A (older EN04 / early EN05, XIAO nRF52840 non-Plus)
 *     Reaches the panel through hidden pogo pins on the underside of the XIAO.
 *     CS=7, DC=16, RST=11, BUSY=3, and a MOSFET panel-power gate on D6.
 *
 *   Generation B (current EN05, XIAO nRF52840 *Plus*)  <-- THIS BOARD
 *     Uses the standard header pins. CS=D1, DC=D3, RST=D0, BUSY=D2, with
 *     hardware SPI on SCK=D8 / MOSI=D10. There is no MOSFET power gate; the
 *     board has a physical power switch instead.
 *
 * Determined empirically on 2026-08-22 with tools/pin_diagnostic. A pull-up /
 * pull-down scan showed D0, D1, D2, D3 all DRIVEN (a device is attached) while
 * D6, D7 and D16 FLOAT (nothing connected). The Generation A mapping therefore
 * cannot work on this board, and GxEPD2 reported "Busy Timeout!" when tried.
 *
 * See docs/hardware/PINOUT.md for the full story.
 */

#ifndef ECLOCK_BOARD_PINS_H
#define ECLOCK_BOARD_PINS_H

// Select the hardware generation. Override with -DECLOCK_EN05_GEN_A=1 if you
// have the older pogo-pin board.
#if !defined(ECLOCK_EN05_GEN_A)
  #define ECLOCK_EN05_GEN_B 1
#endif

#if defined(ECLOCK_EN05_GEN_B)

  // ---- Generation B: XIAO nRF52840 Plus + current EN05 (header pins) ----
  // These indices are identical on both the Adafruit and mbed cores, because
  // D0..D10 are defined consistently in both. That is a welcome side effect of
  // the move away from the pogo pads.
  #define EPD_CS    1     // D1
  #define EPD_DC    3     // D3
  #define EPD_RST   0     // D0
  #define EPD_BUSY  2     // D2
  #define EPD_SCK   8     // D8  - hardware SPI SCK
  #define EPD_MOSI  10    // D10 - hardware SPI MOSI

  // No MOSFET panel-power gate on this generation. Defined as -1 so code can
  // test for it; panelPowerOn() becomes a no-op.
  #define EPD_POWER (-1)

#elif defined(ECLOCK_EN05_GEN_A)

  // ---- Generation A: older EN04 / EN05 via hidden pogo pins ----
  #if defined(ECLOCK_CORE_ADAFRUIT)
    // NOTE: the variant header defines only D0..D10, so D11/D16 do not exist as
    // macros even though the pin *indices* 11 and 16 are correct
    // (g_ADigitalPinMap maps 16 -> P0.27 and 11 -> P0.26). Use bare integers.
    #define EPD_CS    7     // P1.12
    #define EPD_DC    16    // P0.27 - shared with the IMU's I2C SCL
    #define EPD_RST   11    // P0.26 - shared with LED_RED
    #define EPD_BUSY  3     // P0.29
    #define EPD_POWER 6     // P1.11 - MOSFET gate, must be HIGH
  #elif defined(ECLOCK_CORE_MBED)
    #define EPD_CS    7
    #define EPD_DC    15    // P0.27
    #define EPD_RST   11    // P0.26
    #define EPD_BUSY  3
    #define EPD_POWER 6
  #else
    #error "Define ECLOCK_CORE_ADAFRUIT or ECLOCK_CORE_MBED"
  #endif

#endif  // generation select

#if defined(ECLOCK_CORE_MBED)
/**
 * Release P0.26/P0.27 from the TWIM hardware peripheral back to plain GPIO.
 *
 * Only relevant to Generation A, where the panel's DC/RST land on P0.26/P0.27.
 * On the mbed core the hardware I2C peripheral is left enabled on those pins at
 * startup, and an active peripheral overrides GPIO toggling on the nRF52, so
 * the display driver's DC writes are silently swallowed.
 *
 * Harmless on Generation B. Side effect: disables the onboard IMU's I2C bus.
 */
static inline void eclock_release_twim_pins() {
    NRF_TWIM0->ENABLE = 0;
    NRF_TWIM1->ENABLE = 0;
    NRF_TWI0->ENABLE  = 0;
    NRF_TWI1->ENABLE  = 0;
    NRF_GPIO->PIN_CNF[26] = 3;
    NRF_GPIO->PIN_CNF[27] = 3;
}
#endif

// Panel: GDEY029T94 / FPC-A005, 296x128 mono, SSD1680 controller.
// GxEPD2 driver class: GxEPD2_290_T94_V2
#define EPD_WIDTH  296
#define EPD_HEIGHT 128

#endif  // ECLOCK_BOARD_PINS_H
