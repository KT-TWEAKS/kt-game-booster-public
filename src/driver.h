#pragma once
#ifndef KT_DRIVER_H
#define KT_DRIVER_H

#include <windows.h>

namespace kt {

class Driver {
public:
    Driver() = default;
    ~Driver() = default;
    bool Init() { return false; }
    void Cleanup() {}
    bool Active() const { return false; }
    bool Control(DWORD, void*, DWORD, void*, DWORD) { return false; }
};

}

#endif
