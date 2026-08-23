# Phase 5: Hardware Quirks & Font Spacing

## Overview
During the final polish of the eClock firmware, we encountered two very distinct hardware/software integration quirks that caused a freezing bug and a visual layout bug. This document captures the root causes and the solutions.

## Lesson 1: Mbed OS SPI Initialization & GPIO Interrupt Storms
**Problem:**
The top-right user button on the EN05 shield (BUTTON_3) is physically wired to the `D9` pin. This pin is also the default hardware SPI `MISO` line for the nRF52840. While the ePaper display only receives data (it does not use MISO to transmit), the Arduino Mbed OS `SPIClass` takes complete control of the pin when `display.init()` (and inherently `SPI.begin()`) is called.
When `SPI.begin()` initializes the MISO pin, it configures the GPIO pad as an input but *removes* any previously applied internal pull-up resistors. Because the button relies on an active-low `INPUT_PULLUP` configuration, the pin was left floating. This floating state caused environmental noise to trigger the `FALLING` edge interrupt millions of times per second. The resulting interrupt storm starved the main loop of CPU time, causing the clock to completely freeze at the "Syncing..." screen without ever triggering the 30-second timeout.

**Solution:**
We initially attempted to completely detach the interrupt and power down the SPI peripheral (`SPI.end()`) when the clock was idle, but this caused severe Mbed OS crashes (`hard faults`) when manipulating the interrupt allocation table. 
The cleaner, more elegant solution was to realize that the SPI peripheral *does not care* if its MISO input pin has a pull-up resistor. By simply calling `pinMode(BUTTON_3, INPUT_PULLUP)` *after* `display.init()` is called, we force the pull-up resistor back onto the pin. The pin stops floating, the interrupt storm ceases, and the button works perfectly because pressing it cleanly pulls the MISO line to GND.

## Lesson 2: Adafruit_GFX Text Wrapping on Custom Fonts
**Problem:**
At precisely 10:09 (and 00:00), the clock display was missing its final digit (e.g. `10:0` instead of `10:09`). The custom 82pt `Chango` font has extremely wide characters (a `0` is 71px wide with a 73px advance). The total calculated width of `10:09` was roughly 301px. Because the ePaper display is only 296px wide, `Adafruit_GFX` determined that the final digit exceeded the right boundary. Because `setTextWrap(true)` is the default behavior in `Adafruit_GFX`, the library gracefully wrapped the final `9` down to the next "line"—which fell completely off the bottom edge of the display, effectively vanishing.

**Solution:**
Without access to the original TTF font generation script to re-export the font at 78pt, we directly patched the `GFXglyph` font table inside `FontChango82.h`. By reducing the `xAdvance` property of all numeric digits by 8 pixels, we artificially squished the characters closer together. This allowed the widest possible time string (`00:00`) to comfortably fit within the 296px constraint. Additionally, we explicitly disabled text wrapping (`display.setTextWrap(false)`) so that if a string ever strays a pixel out of bounds, it simply clips harmlessly at the edge rather than disappearing.
