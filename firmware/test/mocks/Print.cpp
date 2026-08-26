// Print.cpp — implementations for the minimal Print mock.

#include "Print.h"

#include <cstdio>
#include <cstring>

size_t Print::write(const uint8_t* buf, size_t len) {
    size_t n = 0;
    while (len--) n += write(*buf++);
    return n;
}

size_t Print::write(const char* s) {
    if (!s) return 0;
    return write(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

size_t Print::print(const char* s) { return write(s); }
size_t Print::print(const String& s) { return write(s.c_str()); }
size_t Print::print(char c) { return write(static_cast<uint8_t>(c)); }

size_t Print::print(const __FlashStringHelper* s) {
    // F() is a reinterpret_cast of a RAM string literal on host.
    return write(reinterpret_cast<const char*>(s));
}

size_t Print::print(unsigned char c, int base) {
    (void)base;  // single char ignores base
    return write(static_cast<uint8_t>(c));
}

size_t Print::print(int n, int base) {
    char buf[34];
    const char* fmt;
    switch (base) {
        case HEX: fmt = "%x"; break;
        case OCT: fmt = "%o"; break;
        case BIN: fmt = "%b"; break;  // not portable; fall back below
        default:  fmt = "%d"; break;
    }
    if (base == BIN) {
        // manual binary
        unsigned int u = static_cast<unsigned int>(n);
        char tmp[33];
        int i = 0;
        if (u == 0) tmp[i++] = '0';
        while (u && i < 32) { tmp[i++] = (u & 1) ? '1' : '0'; u >>= 1; }
        int j = 0;
        while (i > 0) buf[j++] = tmp[--i];
        buf[j] = '\0';
        return write(buf);
    }
    snprintf(buf, sizeof(buf), fmt, n);
    return write(buf);
}

size_t Print::print(unsigned int n, int base) {
    char buf[34];
    const char* fmt;
    switch (base) {
        case HEX: fmt = "%x"; break;
        case OCT: fmt = "%o"; break;
        case BIN: fmt = "%b"; break;
        default:  fmt = "%u"; break;
    }
    if (base == BIN) {
        unsigned int u = n;
        char tmp[33];
        int i = 0;
        if (u == 0) tmp[i++] = '0';
        while (u && i < 32) { tmp[i++] = (u & 1) ? '1' : '0'; u >>= 1; }
        int j = 0;
        while (i > 0) buf[j++] = tmp[--i];
        buf[j] = '\0';
        return write(buf);
    }
    snprintf(buf, sizeof(buf), fmt, n);
    return write(buf);
}

size_t Print::print(long n, int base) {
    if (base == DEC) {
        char buf[34];
        snprintf(buf, sizeof(buf), "%ld", n);
        return write(buf);
    }
    return print(static_cast<int>(n), base);
}

size_t Print::print(unsigned long n, int base) {
    if (base == DEC) {
        char buf[34];
        snprintf(buf, sizeof(buf), "%lu", n);
        return write(buf);
    }
    return print(static_cast<unsigned int>(n), base);
}

size_t Print::print(double n, int digits) {
    char buf[40];
    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%.%df", digits);
    snprintf(buf, sizeof(buf), fmt, n);
    return write(buf);
}

size_t Print::println(void) { return write("\r\n"); }
size_t Print::println(const char* s) { size_t n = print(s); return n + println(); }
size_t Print::println(char c) { size_t n = print(c); return n + println(); }
size_t Print::println(int n, int base) { size_t r = print(n, base); return r + println(); }
