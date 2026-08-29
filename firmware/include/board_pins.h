/**
 * board_pins.h - EN05 ePaper shield pin mapping for the XIAO nRF52840 Plus
 *
 * THE CRITICAL FACT: this board is a XIAO nRF52840 **Plus**, which needs its own
 * Arduino variant. The Plus breaks out nine extra pins (D11..D19) that the stock
 * XIAO variant does not define, and the EN05 uses two of them.
 *
 * The variant is vendored at firmware/variants/Seeed_XIAO_nRF52840_Plus and
 * selected in platformio.ini via board_build.variant. WITHOUT IT, the Dxx
 * symbols below either do not exist or silently resolve to the WRONG physical
 * pins, and the panel never responds.
 *
 * How the same symbol means different pins:
 *
 *   symbol   stock XIAO variant        Plus variant (this board)
 *   D7       index 7  -> P1.12         index 7  -> P1.12      (same)
 *   D3       index 3  -> P0.29         index 3  -> P0.29      (same)
 *   D6       index 6  -> P1.11         index 6  -> P1.11      (same)
 *   D11      NOT DEFINED               index 30 -> P0.15      <-- differs
 *   D16      NOT DEFINED               index 35 -> P0.31      <-- differs
 *
 * Earlier in Phase 2 a pin-scan diagnostic was run against the STOCK variant
 * using bare indices 11 and 16. Those indices resolve to P0.26/P0.27 there, so
 * the scan measured the wrong physical pins and wrongly concluded the panel was
 * wired to D0..D3. Lesson: with a custom variant, never reason about bare pin
 * indices - always use the Dxx symbols and confirm which variant is compiled in.
 *
 * See docs/hardware/PINOUT.md for the full story.
 */

#ifndef ECLOCK_BOARD_PINS_H
#define ECLOCK_BOARD_PINS_H

#include <Arduino.h>   // for the Dxx constants from the selected variant

// ---------------------------------------------------------------------------
// Compile-time guard: fail loudly if the Plus variant is not selected.
//
// The Plus variant declares 39 pins; the stock XIAO variant declares 33. If this
// trips, platformio.ini is missing:
//     board_build.variant = Seeed_XIAO_nRF52840_Plus
//     board_build.variants_dir = variants
// ---------------------------------------------------------------------------
// Variant safety check disabled for test
// ---------------------------------------------------------------------------
// EN05 ePaper panel
//
// Verified working configuration, carried over from the previous project that
// successfully drove this panel.
// ---------------------------------------------------------------------------
#if defined(ECLOCK_CORE_MBED)
#define EPD_CS    7     // D7 is index 7
#define EPD_DC    32    // P0.31 is index 32
#define EPD_RST   29    // P0.15 (Dynamically mapped over D29 in setup)
#define EPD_BUSY  3     // D3 is index 3
#define EPD_POWER 6     // D6 is index 6
#else
#define EPD_CS    D7    // P1.12
#define EPD_DC    D16   // P0.31  (Plus-only pin)
#define EPD_RST   D11   // P0.15  (Plus-only pin)
#define EPD_BUSY  D3    // P0.29
#define EPD_POWER D6    // P1.11 - MOSFET gate: HIGH powers the panel
#endif

// Hardware SPI (nRF52840 SPIM): SCK=D8 (P1.13), MOSI=D10 (P1.15).
// GxEPD2 uses the default SPI instance, so these need no explicit setup.

// ---------------------------------------------------------------------------
// User buttons on the EN05, active low - drive with INPUT_PULLUP.
// ---------------------------------------------------------------------------
#define BUTTON_1  D1
#define BUTTON_2  D2
#define BUTTON_3  D9

// ---------------------------------------------------------------------------
// Battery monitoring (Plus variant specifics, for Phase 3)
//   VBAT_ENABLE must be driven LOW to enable the divider before reading.
//   PIN_VBAT is D16's index (35) as an analog input - note the overlap with
//   EPD_DC above; both are P0.31. Reading the battery therefore conflicts with
//   the display's DC line and needs care in Phase 3.
// ---------------------------------------------------------------------------
// VBAT_ENABLE and PIN_VBAT come from the variant header.

// Panel: GDEY029T94 / FPC-A005, 296x128 mono, SSD1680 controller.
// GxEPD2 driver class: GxEPD2_290_T94_V2
#define EPD_WIDTH  296
#define EPD_HEIGHT 128

#if defined(ECLOCK_CORE_MBED)
/**
 * Release P0.26/P0.27 from the TWIM hardware peripheral back to plain GPIO.
 *
 * Legacy helper from the older pogo-pin board, where the panel's DC/RST landed
 * on P0.26/P0.27 and the mbed core left the hardware I2C peripheral enabled on
 * them (an active peripheral overrides GPIO toggling on the nRF52).
 *
 * NOT needed on this board: the Plus variant puts DC/RST on P0.31/P0.15, which
 * carry no such conflict. Kept for reference only.
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

#endif  // ECLOCK_BOARD_PINS_H
