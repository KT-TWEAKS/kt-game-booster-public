#pragma once
#ifndef KT_PLATFORM_H
#define KT_PLATFORM_H

#include <windows.h>

namespace kt {

struct PlatformCapabilities {
    DWORD major = 0;
    DWORD minor = 0;
    DWORD build = 0;
    bool elevated = false;
    bool hypervisor = false;
    bool vbs = false;
    bool hvci = false;
    bool cpuSets = false;
    bool ecoQos = false;
    bool etw = false;
    bool hybridCertain = false;
};

PlatformCapabilities DetectPlatformCapabilities();
void LogPlatformCapabilities(const PlatformCapabilities& caps);

}
#endif
