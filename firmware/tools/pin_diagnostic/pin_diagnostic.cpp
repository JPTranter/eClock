/**
 * eClock - pin diagnostic
 *
 * Purpose: determine the ACTUAL EN05 wiring on this board, empirically.
 *
 * Context: the inherited notes describe an older EN04/EN05 generation that
 * reached the panel through hidden pogo pins (CS=7, DC=16, RST=11, BUSY=3).
 * With that mapping GxEPD2 reports "Busy Timeout!", meaning the BUSY line never
 * deasserts. Vendor documentation for the current XIAO nRF52840 Plus + EN05
 * instead shows standard header pins: CS=D1, DC=D3, RST=D0, BUSY=D2, SCK=D8,
 * MOSI=D10. This sketch decides which is true rather than assuming.
 *
 * Method:
 *   Test A - float every candidate pin with a pull-up, then with a pull-down.
 *            A pin driven by the panel holds its level against both. A floating
 *            (unconnected) pin follows whichever resistor is applied.
 *   Test B - toggle a candidate RST pin and watch the candidate BUSY pins. A
 *            real BUSY line reacts to a panel reset; an unconnected pin does not.
 *
 * This is throwaway diagnostic code, kept in tools/ for future board revisions.
 */

#include <Arduino.h>

#if defined(ECLOCK_CORE_ADAFRUIT)
  #include <Adafruit_TinyUSB.h>
#endif

// Every plausible candidate: D0..D10 header pins, plus the pogo-pin indices
// from the inherited notes (11 = P0.26, 16 = P0.27).
static const uint8_t CANDIDATES[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 16};
static const size_t  N_CANDIDATES = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

static void bannerLine() {
    Serial.println(F("--------------------------------------------------"));
}

/**
 * Test A: classify each pin as driven or floating.
 */
static void scanPinStates() {
    Serial.println();
    bannerLine();
    Serial.println(F("TEST A: pull-up / pull-down response"));
    Serial.println(F("  'driven'   = holds level against both resistors"));
    Serial.println(F("               (something on the board drives it)"));
    Serial.println(F("  'floating' = follows the resistor (unconnected)"));
    bannerLine();
    Serial.println(F("  pin   pullup  pulldown  verdict"));

    for (size_t i = 0; i < N_CANDIDATES; i++) {
        const uint8_t p = CANDIDATES[i];

        pinMode(p, INPUT_PULLUP);
        delay(5);
        const int up = digitalRead(p);

        pinMode(p, INPUT_PULLDOWN);
        delay(5);
        const int down = digitalRead(p);

        pinMode(p, INPUT);   // leave high-impedance

        const char* verdict;
        if (up == down) {
            verdict = (up == HIGH) ? "DRIVEN HIGH" : "DRIVEN LOW";
        } else {
            verdict = "floating";
        }

        Serial.print(F("  D"));
        Serial.print(p);
        if (p < 10) Serial.print(F(" "));
        Serial.print(F("    "));
        Serial.print(up);
        Serial.print(F("       "));
        Serial.print(down);
        Serial.print(F("        "));
        Serial.println(verdict);
    }
    bannerLine();
}

/**
 * Test B: does any candidate BUSY pin react to a reset pulse on rstPin?
 */
static void probeResetReaction(uint8_t rstPin, uint8_t powerPin) {
    Serial.println();
    bannerLine();
    Serial.print(F("TEST B: reset pulse on D"));
    Serial.print(rstPin);
    Serial.print(F(", panel power on D"));
    Serial.println(powerPin);
    bannerLine();

    // Power the panel first - nothing responds without its rail.
    pinMode(powerPin, OUTPUT);
    digitalWrite(powerPin, HIGH);
    delay(100);

    // Sample all candidates before the pulse.
    int before[N_CANDIDATES];
    for (size_t i = 0; i < N_CANDIDATES; i++) {
        if (CANDIDATES[i] == rstPin || CANDIDATES[i] == powerPin) {
            before[i] = -1;
            continue;
        }
        pinMode(CANDIDATES[i], INPUT);
        before[i] = digitalRead(CANDIDATES[i]);
    }

    // Reset pulse: SSD1680 wants RST low then high.
    pinMode(rstPin, OUTPUT);
    digitalWrite(rstPin, HIGH);
    delay(10);
    digitalWrite(rstPin, LOW);
    delay(10);
    digitalWrite(rstPin, HIGH);
    delay(10);   // BUSY should assert around here on a real panel

    Serial.println(F("  pin   before  after   changed?"));
    for (size_t i = 0; i < N_CANDIDATES; i++) {
        if (before[i] < 0) continue;
        const int after = digitalRead(CANDIDATES[i]);

        Serial.print(F("  D"));
        Serial.print(CANDIDATES[i]);
        if (CANDIDATES[i] < 10) Serial.print(F(" "));
        Serial.print(F("    "));
        Serial.print(before[i]);
        Serial.print(F("       "));
        Serial.print(after);
        Serial.print(F("       "));
        Serial.println(after != before[i] ? F("*** CHANGED ***") : F("-"));
    }

    // Watch for BUSY deasserting over the next couple of seconds.
    Serial.println();
    Serial.println(F("  Watching 2s for a late transition (BUSY deassert):"));
    uint32_t t0 = millis();
    bool sawChange = false;
    int last[N_CANDIDATES];
    for (size_t i = 0; i < N_CANDIDATES; i++) {
        last[i] = (before[i] < 0) ? -1 : digitalRead(CANDIDATES[i]);
    }

    while (millis() - t0 < 2000) {
        for (size_t i = 0; i < N_CANDIDATES; i++) {
            if (last[i] < 0) continue;
            const int now = digitalRead(CANDIDATES[i]);
            if (now != last[i]) {
                Serial.print(F("    +"));
                Serial.print(millis() - t0);
                Serial.print(F("ms  D"));
                Serial.print(CANDIDATES[i]);
                Serial.print(F(" -> "));
                Serial.println(now);
                last[i] = now;
                sawChange = true;
            }
        }
        delay(1);
    }
    if (!sawChange) {
        Serial.println(F("    (no transitions)"));
    }

    digitalWrite(powerPin, LOW);
    bannerLine();
}

static void printMenu() {
    Serial.println();
    Serial.println(F("=================================================="));
    Serial.println(F("  eClock pin diagnostic"));
    Serial.println(F("=================================================="));
    Serial.println(F("  a  Test A: pull-up/pull-down scan"));
    Serial.println(F("  1  Test B: reset on D0  (vendor-documented RST)"));
    Serial.println(F("  2  Test B: reset on D11 (inherited-notes RST)"));
    Serial.println(F("  ?  menu"));
    Serial.println(F("=================================================="));
}

void setup() {
    Serial.begin(115200);
    uint32_t start = millis();
    while (!Serial && (millis() - start) < 3000) delay(10);

    Serial.println();
    Serial.println(F("eClock pin diagnostic"));
    Serial.print(F("Built: "));
    Serial.print(F(__DATE__));
    Serial.print(F(" "));
    Serial.println(F(__TIME__));
    printMenu();

    // Run the scan automatically; it is read-only and safe.
    scanPinStates();
}

void loop() {
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c == '\r' || c == '\n') continue;
        switch (c) {
            case 'a': scanPinStates();      break;
            case '1': probeResetReaction(0, 6);  break;   // vendor pinout guess
            case '2': probeResetReaction(11, 6); break;   // inherited notes
            case '?': printMenu();          break;
            default: break;
        }
    }
}
