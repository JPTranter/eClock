#pragma once

// ---------------------------------------------------------------------------
// GxEPD2_BW.h — host-test mock for the GxEPD2 ePaper driver.
//
// The real GxEPD2_BW<Page, page_height> derives from Adafruit_GFX and manages
// a page buffer + SPI transfers. On host we replace all of that with a plain
// GFXcanvas1 framebuffer sized to the LOGICAL (rotated) panel dimensions:
// 296 wide x 128 tall (the panel is physically 128x296 portrait, driven in
// landscape via setRotation(1)).
//
// GFXcanvas1 rasterizes the real fonts (FontTitanOne.h, FreeSans) and the
// material_icons bitmaps through Adafruit_GFX, so the buffer is byte-for-byte
// what the device would render. getBuffer() is exposed for PNG dumps.
//
// Fidelity notes (verified against GxEPD2 source):
//   * GFXcanvas1 uses MSB-first bit order, bit set = white (GxEPD_WHITE),
//     matching Pillow '1'-mode and the SSD1680 (1=white).
//   * firstPage()/nextPage() execute the draw body exactly once (page_height
//     == HEIGHT on this panel -> single page).
// ---------------------------------------------------------------------------

#include <cstdint>

// Color constants (match GxEPD2.h)
#define GxEPD_BLACK 0x0000
#define GxEPD_WHITE 0xFFFF

// The vendored GFX canvas (derives from Adafruit_GFX).
#include "Adafruit_GFX.h"

// Panel class (matches how main.cpp instantiates the template):
//   GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(
//       GxEPD2_290_T94_V2(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
class GxEPD2_290_T94_V2 {
public:
    static const uint16_t WIDTH = 128;          // physical (portrait native)
    static const uint16_t WIDTH_VISIBLE = 128;
    static const uint16_t HEIGHT = 296;
    GxEPD2_290_T94_V2(int16_t cs, int16_t dc, int16_t rst, int16_t busy) {
        (void)cs; (void)dc; (void)rst; (void)busy;
    }
};

// Logical (post-rotation) dimensions the firmware draws in.
static const int16_t EPD_LOGICAL_W = 296;
static const int16_t EPD_LOGICAL_H = 128;

template <typename Page, uint16_t page_height>
class GxEPD2_BW : public GFXcanvas1 {
public:
    GxEPD2_BW(Page) : GFXcanvas1(EPD_LOGICAL_W, EPD_LOGICAL_H) { _armed = false; }

    // GxEPD2 lifecycle (all no-ops; the framebuffer is already live).
    void init(uint32_t baud, bool initial, uint16_t reset_pulse, bool pulldown_rst_mode) {
        (void)baud; (void)initial; (void)reset_pulse; (void)pulldown_rst_mode;
    }
    void setRotation(uint8_t r) {
        // Canvas is already in the logical 296x128 landscape orientation.
        (void)r;
    }
    void setPartialWindow(int16_t x, int16_t y, int16_t w, int16_t h) {
        (void)x; (void)y; (void)w; (void)h;
    }
    void setFullWindow() {}

    // Page loop: run the draw body exactly once (matches single-page panel).
    void firstPage() { _armed = true; }
    bool nextPage() {
        if (!_armed) return false;
        _armed = false;
        return true;
    }

    void powerOff() {}

private:
    bool _armed;
};
