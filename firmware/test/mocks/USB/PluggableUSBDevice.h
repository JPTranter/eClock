#pragma once

class PluggableUSBDevice_Mock {
public:
    void deinit() {}
};

inline PluggableUSBDevice_Mock& PluggableUSBD() {
    static PluggableUSBDevice_Mock mock;
    return mock;
}
