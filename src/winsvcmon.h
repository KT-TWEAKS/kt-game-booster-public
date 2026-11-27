#pragma once
#ifndef KT_WINSVCMON_H
#define KT_WINSVCMON_H

#include <windows.h>
#include <string>
#include <vector>

namespace kt {

struct ServiceInfo {
    wchar_t name[64]{};
    wchar_t displayName[96]{};
    DWORD    state = 0;
    DWORD    pid = 0;
    int      canStop = 0;
    int      canPause = 0;
    int      gamingTarget = 0;
    int      protectedService = 0;
};

int EnumServices(std::vector<ServiceInfo>& out);

bool ServiceStart(const std::wstring& name);
bool ServiceStop(const std::wstring& name);
bool ServicePause(const std::wstring& name);
bool ServiceResume(const std::wstring& name);

bool IsGamingServiceTarget(const std::wstring& name);
bool IsProtectedService(const std::wstring& name);

}

#endif
