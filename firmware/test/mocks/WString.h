#pragma once

// ---------------------------------------------------------------------------
// WString.h — minimal String for host tests.
// Only what main.cpp + Adafruit_GFX use: ctor(const char*), copy, c_str(),
// assignment. Implemented inline (no .cpp needed).
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdlib>
#include <cstring>

class String {
public:
    String() : _s(nullptr), _len(0) {}
    String(const char* s) { init(s); }
    String(const String& o) { init(o._s); }
    ~String() { free(_s); }

    String& operator=(const String& o) {
        if (this != &o) {
            char* ns = dup(o._s);
            free(_s);
            _s = ns;
            _len = o._len;
        }
        return *this;
    }

    const char* c_str() const { return _s ? _s : ""; }
    unsigned int length() const { return _len; }

private:
    char* _s;
    unsigned int _len;

    static char* dup(const char* s) {
        if (!s) return nullptr;
        size_t n = strlen(s) + 1;
        char* d = static_cast<char*>(malloc(n));
        if (d) memcpy(d, s, n);
        return d;
    }
    void init(const char* s) {
        _s = dup(s);
        _len = _s ? static_cast<unsigned int>(strlen(_s)) : 0;
    }
};
