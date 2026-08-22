/**
 * eClock - Phase 2: display functionality
 *
 * An interactive, serial-driven test harness for the 2.9" SSD1680 ePaper panel.
 * Phase 2's real deliverable is not "text on screen" but *characterisation*: how
 * partial refresh behaves, how fast it is, and how many partials can run before
 * ghosting forces a full refresh. That needs repeatable operator-driven runs, so
 * this is a menu rather than a fixed demo.
 *
 * Send a single character over serial to run a command; press '?' for the menu.
 *
 * Panel: GDEY029T94 / FPC-A005, 296x128 mono, SSD1680 controller.
 * See docs/hardware/PINOUT.md before changing any pin or power handling here.
 */

#include <Arduino.h>
#include "board_pins.h"

// See the Phase 1 notes: on the Adafruit core `Serial` is TinyUSB CDC and the
// build fails at link time without this include.
#if defined(ECLOCK_CORE_ADAFRUIT)
  #include <Adafruit_TinyUSB.h>
#endif

#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

// GxEPD2_290_T94_V2 maps to the SSD1680 controller on this panel.
// The second template parameter is the page-buffer height; using the full
// panel HEIGHT gives a single-page buffer (296*128/8 = 4736 bytes), which fits
// comfortably in 232KB of RAM and avoids multi-page complexity.
GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(
    GxEPD2_290_T94_V2(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

static const uint32_t SERIAL_WAIT_MS = 3000;

static bool     g_displayReady   = false;
static uint32_t g_partialCount   = 0;   // partials since the last full refresh
static uint32_t g_fullCount      = 0;
static uint32_t g_lastOpMs       = 0;   // duration of the last refresh operation

// ---------------------------------------------------------------------------
// Panel power
//
// The EN05 gates the panel supply with a MOSFET on EPD_POWER (D6 = P1.11).
// Nothing on the SPI bus matters until this is HIGH: the panel will accept
// writes and do nothing, which reads exactly like a software bug.
//
// Kept as explicit calls because Phase 3 will want to measure the cost of
// cutting this rail between updates.
// ---------------------------------------------------------------------------
static void panelPowerOn() {
    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(50);   // let the rail settle before driving RST/SPI
}

static void panelPowerOff() {
    digitalWrite(EPD_POWER, LOW);
}

static void displayInit() {
    Serial.println(F("[disp] powering panel (EPD_POWER HIGH)"));
    panelPowerOn();

    Serial.println(F("[disp] display.init()"));
    // init(serial_diag_baudrate, initial_refresh, reset_pulse_ms, pulldown_rst)
    // initial_refresh=true forces a full clear so we start from a known state.
    display.init(115200, true, 2, false);
    display.setRotation(1);        // landscape: 296 wide x 128 tall

    g_displayReady = true;
    g_partialCount = 0;

    Serial.print(F("[disp] ready. w="));
    Serial.print(display.width());
    Serial.print(F(" h="));
    Serial.print(display.height());
    Serial.print(F(" pages="));
    Serial.println(display.pages());
}

static bool requireDisplay() {
    if (!g_displayReady) {
        Serial.println(F("[warn] display not initialised - press 'i' first"));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test patterns
// ---------------------------------------------------------------------------

/** Full-window refresh with a geometry/alignment pattern. */
static void drawTestPattern() {
    if (!requireDisplay()) return;

    uint32_t t0 = millis();
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Border: proves we know the true panel bounds in this rotation.
        display.drawRect(0, 0, display.width(), display.height(), GxEPD_BLACK);

        // Corner markers: catch off-by-one and rotation errors.
        const int16_t m = 12;
        display.fillRect(0, 0, m, m, GxEPD_BLACK);
        display.fillRect(display.width() - m, 0, m, m, GxEPD_BLACK);
        display.fillRect(0, display.height() - m, m, m, GxEPD_BLACK);
        display.fillRect(display.width() - m, display.height() - m, m, m, GxEPD_BLACK);

        // Centre crosshair.
        display.drawLine(display.width() / 2, 0,
                         display.width() / 2, display.height(), GxEPD_BLACK);
        display.drawLine(0, display.height() / 2,
                         display.width(), display.height() / 2, GxEPD_BLACK);

        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeSans9pt7b);
        display.setCursor(20, 30);
        display.print(F("eClock Phase 2"));
        display.setCursor(20, 50);
        display.print(display.width());
        display.print(F("x"));
        display.print(display.height());
    } while (display.nextPage());

    g_lastOpMs = millis() - t0;
    g_fullCount++;
    g_partialCount = 0;

    Serial.print(F("[full] test pattern in "));
    Serial.print(g_lastOpMs);
    Serial.println(F(" ms"));
}

/** Full white clear. */
static void doClear() {
    if (!requireDisplay()) return;

    uint32_t t0 = millis();
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
    } while (display.nextPage());

    g_lastOpMs = millis() - t0;
    g_fullCount++;
    g_partialCount = 0;

    Serial.print(F("[full] clear in "));
    Serial.print(g_lastOpMs);
    Serial.println(F(" ms"));
}

/**
 * Draw a mock clock face. `partial` selects a partial-window update, which is
 * what the real clock will do once a minute.
 *
 * The time shown is derived from uptime, not a real clock - Phase 4 supplies
 * real time over BLE. The point here is to exercise a realistic glyph load.
 */
static void drawClockFace(bool partial) {
    if (!requireDisplay()) return;

    const uint32_t secs  = millis() / 1000;
    const uint8_t  hh    = (uint8_t)((secs / 3600) % 24);
    const uint8_t  mm    = (uint8_t)((secs / 60) % 60);

    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", hh, mm);

    uint32_t t0 = millis();

    if (partial) {
        // Full-window partial update. GxEPD2 handles the differential/
        // double-buffer logic; this is the fast, non-flashing path.
        display.setPartialWindow(0, 0, display.width(), display.height());
    } else {
        display.setFullWindow();
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Big time, centred.
        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeSansBold24pt7b);
        int16_t bx, by;
        uint16_t bw, bh;
        display.getTextBounds(timeBuf, 0, 0, &bx, &by, &bw, &bh);
        display.setCursor((display.width() - bw) / 2 - bx,
                          (display.height() + bh) / 2 - 8);
        display.print(timeBuf);

        // Status line: refresh counters, so ghosting can be correlated with
        // exactly how many partials have accumulated.
        display.setFont(&FreeSans9pt7b);
        display.setCursor(4, display.height() - 5);
        display.print(partial ? F("P") : F("F"));
        display.print(F(" p="));
        display.print(g_partialCount + (partial ? 1 : 0));
        display.print(F(" f="));
        display.print(g_fullCount + (partial ? 0 : 1));

        // A thin baseline: fine detail is where ghosting shows up first.
        display.drawLine(0, display.height() - 20,
                         display.width(), display.height() - 20, GxEPD_BLACK);
    } while (display.nextPage());

    g_lastOpMs = millis() - t0;
    if (partial) {
        g_partialCount++;
    } else {
        g_fullCount++;
        g_partialCount = 0;
    }

    Serial.print(partial ? F("[part] ") : F("[full] "));
    Serial.print(timeBuf);
    Serial.print(F(" in "));
    Serial.print(g_lastOpMs);
    Serial.print(F(" ms  partials_since_full="));
    Serial.println(g_partialCount);
}

/**
 * Ghosting characterisation: run a batch of partial updates back to back,
 * reporting timing for each. Inspect the glass afterwards and note where
 * ghosting became objectionable.
 */
static void runPartialBurst(uint16_t n) {
    if (!requireDisplay()) return;

    Serial.print(F("[burst] "));
    Serial.print(n);
    Serial.println(F(" partial refreshes - watch the panel for ghosting"));

    uint32_t total = 0;
    uint32_t worst = 0;

    for (uint16_t i = 0; i < n; i++) {
        const uint32_t t0 = millis();

        display.setPartialWindow(0, 0, display.width(), display.height());
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);
            display.setTextColor(GxEPD_BLACK);
            display.setFont(&FreeSansBold24pt7b);

            char buf[8];
            snprintf(buf, sizeof(buf), "%03u", (unsigned)(g_partialCount + 1));
            display.setCursor(90, 80);
            display.print(buf);

            display.setFont(&FreeSans9pt7b);
            display.setCursor(4, display.height() - 5);
            display.print(F("burst partial #"));
            display.print(g_partialCount + 1);
        } while (display.nextPage());

        const uint32_t dt = millis() - t0;
        total += dt;
        if (dt > worst) worst = dt;
        g_partialCount++;

        Serial.print(F("  #"));
        Serial.print(g_partialCount);
        Serial.print(F(" "));
        Serial.print(dt);
        Serial.println(F(" ms"));
    }

    Serial.print(F("[burst] done. n="));
    Serial.print(n);
    Serial.print(F(" avg="));
    Serial.print(total / (n ? n : 1));
    Serial.print(F(" ms worst="));
    Serial.print(worst);
    Serial.print(F(" ms total_partials_since_full="));
    Serial.println(g_partialCount);
    Serial.println(F("[burst] inspect the glass; press 'f' for a full refresh"));
}

/** Font size comparison, for choosing the clock face typography. */
static void drawFontSamples() {
    if (!requireDisplay()) return;

    uint32_t t0 = millis();
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        display.setFont(&FreeSans9pt7b);
        display.setCursor(4, 16);
        display.print(F("FreeSans9  08:42 battery 87%"));

        display.setFont(&FreeSans12pt7b);
        display.setCursor(4, 42);
        display.print(F("FreeSans12  08:42"));

        display.setFont(&FreeSansBold24pt7b);
        display.setCursor(4, 100);
        display.print(F("08:42"));
    } while (display.nextPage());

    g_lastOpMs = millis() - t0;
    g_fullCount++;
    g_partialCount = 0;

    Serial.print(F("[full] font samples in "));
    Serial.print(g_lastOpMs);
    Serial.println(F(" ms"));
}

static void doHibernate() {
    if (!requireDisplay()) return;
    Serial.println(F("[disp] hibernate() - panel to deep sleep"));
    display.hibernate();
    Serial.println(F("[disp] hibernated. Next draw needs 'i' if it does not wake."));
}

static void printStats() {
    Serial.println(F("--- stats ---"));
    Serial.print(F("  pins CS/DC/RST/BUSY  : "));
    Serial.print(EPD_CS);   Serial.print(F("/"));
    Serial.print(EPD_DC);   Serial.print(F("/"));
    Serial.print(EPD_RST);  Serial.print(F("/"));
    Serial.println(EPD_BUSY);
    Serial.print(F("  power gate           : D6 -> "));
    Serial.println(EPD_POWER);
    Serial.print(F("  BUSY pin reads       : "));
    pinMode(EPD_BUSY, INPUT);
    Serial.print(digitalRead(EPD_BUSY));
    Serial.println(F("  (SSD1680: 1 = busy, 0 = idle)"));
    Serial.print(F("  initialised          : "));
    Serial.println(g_displayReady ? F("yes") : F("no"));
    Serial.print(F("  full refreshes       : "));
    Serial.println(g_fullCount);
    Serial.print(F("  partials since full  : "));
    Serial.println(g_partialCount);
    Serial.print(F("  last operation       : "));
    Serial.print(g_lastOpMs);
    Serial.println(F(" ms"));
    Serial.print(F("  uptime               : "));
    Serial.print(millis() / 1000);
    Serial.println(F(" s"));
}

static void printMenu() {
    Serial.println();
    Serial.println(F("=============================================="));
    Serial.println(F("  eClock Phase 2 - display test harness"));
    Serial.println(F("=============================================="));
    Serial.println(F("  i  init display (full clear)"));
    Serial.println(F("  t  full refresh: geometry test pattern"));
    Serial.println(F("  c  full refresh: clear to white"));
    Serial.println(F("  n  full refresh: clock face"));
    Serial.println(F("  p  PARTIAL refresh: clock face"));
    Serial.println(F("  b  burst of 10 partial refreshes"));
    Serial.println(F("  B  burst of 50 partial refreshes"));
    Serial.println(F("  k  font samples"));
    Serial.println(F("  h  hibernate panel"));
    Serial.println(F("  o  panel power OFF (cut the D6 rail)"));
    Serial.println(F("  s  stats"));
    Serial.println(F("  ?  this menu"));
    Serial.println(F("=============================================="));
    Serial.println();
}

void setup() {
#if defined(ECLOCK_CORE_MBED)
    // Must run before anything touches P0.26/P0.27 - see board_pins.h.
    eclock_release_twim_pins();
#endif

    Serial.begin(115200);
    uint32_t start = millis();
    while (!Serial && (millis() - start) < SERIAL_WAIT_MS) {
        delay(10);
    }

    Serial.println();
    Serial.println(F("eClock Phase 2 - display functionality"));
    Serial.print(F("Built: "));
    Serial.print(F(__DATE__));
    Serial.print(F(" "));
    Serial.println(F(__TIME__));
    Serial.print(F("Panel pins: CS="));   Serial.print(EPD_CS);
    Serial.print(F(" DC="));              Serial.print(EPD_DC);
    Serial.print(F(" RST="));             Serial.print(EPD_RST);
    Serial.print(F(" BUSY="));            Serial.print(EPD_BUSY);
    Serial.print(F(" PWR="));             Serial.println(EPD_POWER);

    printMenu();
    Serial.println(F("[boot] auto-initialising display..."));
    displayInit();
    drawTestPattern();
    Serial.println(F("[boot] if the panel is blank, see the checklist in"));
    Serial.println(F("[boot] docs/hardware/PINOUT.md ('Diagnosing a non-responsive panel')"));
}

void loop() {
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c == '\r' || c == '\n') continue;

        switch (c) {
            case 'i': displayInit();          break;
            case 't': drawTestPattern();      break;
            case 'c': doClear();              break;
            case 'n': drawClockFace(false);   break;
            case 'p': drawClockFace(true);    break;
            case 'b': runPartialBurst(10);    break;
            case 'B': runPartialBurst(50);    break;
            case 'k': drawFontSamples();      break;
            case 'h': doHibernate();          break;
            case 'o':
                panelPowerOff();
                g_displayReady = false;
                Serial.println(F("[disp] panel rail off; press 'i' to bring it back"));
                break;
            case 's': printStats();           break;
            case '?': printMenu();            break;
            default:
                Serial.print(F("[warn] unknown command '"));
                Serial.print((char)c);
                Serial.println(F("' - press '?' for the menu"));
                break;
        }
    }
}
