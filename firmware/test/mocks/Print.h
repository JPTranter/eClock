#pragma once

// ---------------------------------------------------------------------------
// Print.h — minimal Arduino Print base class for host tests.
// Adafruit_GFX derives from Print and relies on print()/write() overloads.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>
#include "WString.h"

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

// Forward-declared so the F() overload resolves (matches Arduino's Print).
class __FlashStringHelper;

class Print {
public:
    virtual ~Print() {}

    // The one pure-virtual that subclasses (Adafruit_GFX) must implement.
    virtual size_t write(uint8_t) = 0;

    // Byte/string writers.
    size_t write(const uint8_t* buf, size_t len);
    size_t write(const char* s);

    // print() overloads.
    size_t print(const char* s);
    size_t print(const String& s);
    size_t print(char c);
    size_t print(const __FlashStringHelper* s);
    size_t print(unsigned char c, int base = DEC);
    size_t print(int n, int base = DEC);
    size_t print(unsigned int n, int base = DEC);
    size_t print(long n, int base = DEC);
    size_t print(unsigned long n, int base = DEC);
    size_t print(double n, int digits = 2);

    // println() overloads (not used by main.cpp, but cheap to provide).
    size_t println(void);
    size_t println(const char* s);
    size_t println(char c);
    size_t println(int n, int base = DEC);
};
