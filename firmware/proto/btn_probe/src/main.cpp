// ============================================================================
// btn_probe — hardware probe for the "one dead eClock button" issue (v6).
//
//   v6 reflects the hard lessons from v1..v5:
//     - v4 wrote NRF_GPIO PIN_CNF on the SPI MISO pin D9/P1.14 to "reclaim" it;
//       that broke the panel (Busy Timeout / blank screen). NO MORE GPIO /
//       SPI-pin writes here. This probe only READS buttons and DRAWS.
//     - The display init MUST mirror the shipping main.cpp exactly:
//         display.init(115200, true, 2, false);   // does the full white refresh
//         display.setRotation(1);
//       Adding an extra clearScreen()/display(false) caused a boot Busy Timeout.
//     - All drawing goes through the proven paged partial pattern from the
//       shipping drawClockFace():
//         display.setPartialWindow(0,0,W,H); firstPage();
//         do { fillScreen(WHITE); draw(...); } while (nextPage());
//       No per-draw powerOff(), no manual clears.
//
//   PURPOSE (unchanged): report per-button Interrupt(I) + Poll(P) counters on a
//   clean readable screen + 115200 serial, reproducing the production boot
//   (display.init before buttons) so the D9 interrupt-missing behaviour is
//   visible. (Recorded in docs/lessons/LESSONS_LEARNT.md #40.)
//
// DISCLAIMER: DEV TOOL, OFF src/, no unit tests. Keep as a lessons-learnt tool.
// ============================================================================

#include <Arduino.h>
#include <mbed.h>
#include "pinDefinitions.h"      // PinDescription, P0_15 (for the RST remap)
#include <GxEPD2_BW.h>

#ifndef ECLOCK_CORE_MBED
#define ECLOCK_CORE_MBED 1
#endif
#include "board_pins.h"   // BUTTON_1/2/3 = D1/D2/D9, EPD_* pins

// Display — same driver + pins as the shipped clock.
GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(
    GxEPD2_290_T94_V2(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ---------------------------------------------------------------------------
// Buttons — per-button Interrupt(I) + Poll(P) counters.
// ---------------------------------------------------------------------------
enum Btn { K1, K2, K3, NBUTTONS };
static const int         kPin[NBUTTONS]  = { BUTTON_1, BUTTON_2, BUTTON_3 };   // D1, D2, D9
static const char* const kName[NBUTTONS] = { "D1", "D2", "D9" };

volatile uint32_t g_irq[NBUTTONS]   = {0};
volatile uint32_t g_poll[NBUTTONS]  = {0};
static uint8_t    g_last_irq_ms[NBUTTONS]  = {0};
static uint32_t   g_last_poll_ms[NBUTTONS] = {0};
static uint32_t   g_now_ms = 0;
static const uint32_t DEBOUNCE_MS = 40;

#define ISR(N)                                                                 \
    static void isr_##N() {                                                     \
        uint32_t now = (uint32_t) millis();                                     \
        if (now - g_last_irq_ms[N] > DEBOUNCE_MS) {                             \
            g_last_irq_ms[N] = (uint8_t) now;                                   \
            ++g_irq[N];                                                         \
        }                                                                       \
    }
ISR(K1)
ISR(K2)
ISR(K3)
static void (*const kIsr[NBUTTONS])(void) = { isr_K1, isr_K2, isr_K3 };

static void initButtons() {
    for (int i = 0; i < NBUTTONS; ++i) {
        pinMode(kPin[i], INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(kPin[i]), kIsr[i], FALLING);
    }
}

static void pollButtons() {
    for (int i = 0; i < NBUTTONS; ++i) {
        if (digitalRead(kPin[i]) == LOW) {          // active-low pressed
            if (g_now_ms - g_last_poll_ms[i] >= DEBOUNCE_MS) {
                g_last_poll_ms[i] = g_now_ms;
                ++g_poll[i];
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Draw — mirrors the shipping drawClockFace() exactly:
//   full-frame partial window + paged loop. Repeated draws = refresh.
// ---------------------------------------------------------------------------
static void draw() {
    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(2);                 // ~10x14 px glyphs

        int y = 6;
        display.setCursor(2, y); display.print("Button probe");              y += 14;
        display.setCursor(2, y); display.print("SPI-first: D9 wake?");       y += 14;
        display.setCursor(2, y); display.print("I=IRQ  P=Poll");              y += 14;

        for (int i = 0; i < NBUTTONS; ++i) {
            char ln[40];
            snprintf(ln, sizeof ln, "%s I=%lu P=%lu", kName[i],
                     (unsigned long) g_irq[i], (unsigned long) g_poll[i]);
            display.setCursor(2, y);
            display.print(ln);
            y += 14;
        }
        if (y + 14 < display.height()) {
            display.setCursor(2, y);
            display.print("Press ~1s each");
        }
    } while (display.nextPage());
}

void setup() {
    g_now_ms = (uint32_t) millis();

    // On the mbed core the panel RST pin must be remapped to P0.15 BEFORE the
        // display inits, or GxEPD2 can't reset the panel and you get "Busy Timeout!"
        // (See PINOUT.md "Verifying the pin mapping" + the shipped main.cpp setup.)
        extern PinDescription g_APinDescription[];
        g_APinDescription[29].name = P0_15;

        // Exactly like the shipping clock:
        pinMode(EPD_POWER, OUTPUT);
        digitalWrite(EPD_POWER, HIGH);
        delay(50);
        display.init(115200, true, 2, false);   // full white refresh included
        display.setRotation(1);

        initButtons();

    Serial.begin(115200);
    Serial.println("btn_probe v6: SPI-first, display mirrored from shipped.");
    delay(200);

    draw();   // first paint: full-frame partial (clears + draws)
}

void loop() {
    g_now_ms = (uint32_t) millis();

    static uint32_t lastPoll = 0;
    if (g_now_ms - lastPoll >= 10) { lastPoll = g_now_ms; pollButtons(); }

    static uint32_t lastLog = 0;
    if (g_now_ms - lastLog >= 1000) {
        lastLog = g_now_ms;
        for (int i = 0; i < NBUTTONS; ++i) {
            char ln[48];
            snprintf(ln, sizeof ln, "%s I=%lu P=%lu",
                     kName[i], (unsigned long) g_irq[i], (unsigned long) g_poll[i]);
            Serial.println(ln);
        }
    }

    // Redraw only when a count changed.
    static bool first = true;
    static uint32_t isrSnap[NBUTTONS] = {0}, pollSnap[NBUTTONS] = {0};
    bool changed = first;
    for (int i = 0; i < NBUTTONS; ++i) {
        if (g_irq[i] != isrSnap[i] || g_poll[i] != pollSnap[i]) changed = true;
        isrSnap[i] = g_irq[i];
        pollSnap[i] = g_poll[i];
    }
    if (changed) { first = false; draw(); }

    delay(5);
}
