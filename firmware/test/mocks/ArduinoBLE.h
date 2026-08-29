#pragma once

// ---------------------------------------------------------------------------
// ArduinoBLE.h — host-test mock for the ArduinoBLE library.
//
// Models the surface main.cpp uses, faithfully reproducing the semantics that
// matter to the state machine:
//
//   * BLECharacteristic::writeValue(buf, len)  (public API)  -> stores value,
//     does NOT set the written flag. This matches the real library, where
//     setup() initialises via writeValue(init_val, 8) without triggering a sync.
//   * A REMOTE write (HA over the air) sets the written flag AND the value.
//     On the real stack this is BLELocalCharacteristic::writeValue(device,...);
//     tests trigger it via test_simulate_remote_write().
//   * written() returns the flag and CLEARS it (matches BLELocalCharacteristic).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstring>
#include <string>

#include "Arduino.h"   // for String

// Characteristic properties (bit flags)
#define BLEBroadcast           0x01
#define BLERead                0x02
#define BLEWriteWithoutResponse 0x04
#define BLEWrite               0x08
#define BLENotify              0x10
#define BLEIndicate            0x20

class BLEService;
class BLECharacteristic;

// ---------------------------------------------------------------------------
// BLECharacteristic
// ---------------------------------------------------------------------------
class BLECharacteristic {
public:
    BLECharacteristic() : _written(false), _valueLength(0), _valueSize(0), _value(nullptr) {}
    BLECharacteristic(const char* uuid, uint8_t properties, int valueSize)
        : _written(false), _valueLength(0), _valueSize(valueSize), _value(new uint8_t[valueSize]) {
        (void)uuid; (void)properties;
        memset(_value, 0, valueSize);
    }

    // Public local API (used by setup()): store value, do NOT set written flag.
    int writeValue(const uint8_t value[], int length) {
        if (!_value) return 0;
        int n = (length < _valueSize) ? length : _valueSize;
        memcpy(_value, value, n);
        _valueLength = n;
        return n;
    }

    // Remote-write simulation (HA over the air): sets written flag + value.
    void test_simulate_remote_write(const uint8_t value[], int length) {
        writeValue(value, length);
        _written = true;
    }

    bool written() { bool w = _written; _written = false; return w; }
    const uint8_t* value() const { return _value; }
    int valueLength() const { return _valueLength; }
    int valueSize() const { return _valueSize; }
    uint8_t operator[](int offset) const { return _value[offset]; }

private:
    bool _written;
    int  _valueLength;
    int  _valueSize;
    uint8_t* _value;
};

// ---------------------------------------------------------------------------
// BLEService
// ---------------------------------------------------------------------------
class BLEService {
public:
    BLEService() {}
    BLEService(const char* uuid) { (void)uuid; }
    void addCharacteristic(BLECharacteristic& c) {
        if (_count < MAX_CHARS) { _chars[_count++] = &c; }
    }
private:
    static const int MAX_CHARS = 8;
    BLECharacteristic* _chars[MAX_CHARS];
    int _count = 0;
};

// ---------------------------------------------------------------------------
// BLEDevice — the global BLE object
// ---------------------------------------------------------------------------
class BLEDevice {
public:
    BLEDevice() : _begin_result(true), _advertising(false), _mac("AA:BB:CC:DD:EE:FF") {}

    // Test scripting
    void test_set_begin_result(bool ok) { _begin_result = ok; }
    void test_set_address(const char* mac) { _mac = mac; }
    bool test_advertising() const { return _advertising; }
    int  test_adv_count() const { return _adv_count; }

    // API surface used by main.cpp
    bool begin() { return _begin_result; }
    void poll() {}
    void end() {}
    void setLocalName(const char*) {}
    void setDeviceName(const char*) {}
    void setAdvertisedService(BLEService&) {}
    void setManufacturerData(const uint8_t*, int) {}
    void addService(BLEService&) {}
    void advertise() { _advertising = true; _adv_count++; }
    void stopAdvertise() { _advertising = false; }
    String address() const { return String(_mac.c_str()); }

private:
    bool _begin_result;
    bool _advertising;
    int  _adv_count = 0;
    std::string _mac;
};

// Global BLE device (matches ArduinoBLE's `extern BLEDevice BLE;`)
extern BLEDevice BLE;
