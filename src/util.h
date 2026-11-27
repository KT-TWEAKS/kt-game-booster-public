#pragma once
#ifndef KT_UTIL_H
#define KT_UTIL_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cwchar>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace kt {

void Log(const wchar_t* module, const wchar_t* fmt, ...);

typedef void (*LogSinkFn)(const wchar_t* line);
void SetLogSink(LogSinkFn fn);

bool EnableDebugPrivilege();

HANDLE AcquireSingleInstance();

void SetTimerResolution(bool on);

inline std::wstring WLower(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) if (c >= L'A' && c <= L'Z') c += 32;
    return r;
}
inline bool IEquals(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
inline bool EndsWithI(const std::wstring& s, const std::wstring& suffix) {
    if (suffix.size() > s.size()) return false;
    return _wcsicmp(s.c_str() + (s.size() - suffix.size()), suffix.c_str()) == 0;
}
bool WildMatch(const wchar_t* pattern, const wchar_t* text);

std::wstring GetProcessNameById(DWORD pid);
std::wstring GetProcessNameByHandle(HANDLE h);
std::wstring GetProcPathByHandle(HANDLE h);
bool GetProcessModuleBaseNameSafe(DWORD pid, wchar_t* out, size_t len);
bool IsProcessRunning(DWORD pid);

bool GuidFromString(const wchar_t* s, GUID* out);

typedef LONG(NTAPI* NtQueryInformationThread_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef LONG(NTAPI* NtSetInformationThread_t)(HANDLE, ULONG, PVOID, ULONG);
typedef LONG(NTAPI* NtSuspendThread_t)(HANDLE, PULONG);
typedef LONG(NTAPI* NtResumeThread_t)(HANDLE, PULONG);
typedef LONG(NTAPI* NtSuspendProcess_t)(HANDLE);
typedef LONG(NTAPI* NtResumeProcess_t)(HANDLE);
typedef LONG(NTAPI* NtQueryInformationProcess_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef LONG(NTAPI* NtSetInformationProcess_t)(HANDLE, ULONG, PVOID, ULONG);

struct NtApi {
    NtQueryInformationThread_t NtQueryInformationThread = nullptr;
    NtSetInformationThread_t   NtSetInformationThread   = nullptr;
    NtSuspendThread_t          NtSuspendThread          = nullptr;
    NtResumeThread_t           NtResumeThread           = nullptr;
    NtSuspendProcess_t         NtSuspendProcess         = nullptr;
    NtResumeProcess_t          NtResumeProcess          = nullptr;
    NtQueryInformationProcess_t NtQueryInformationProcess = nullptr;
    NtSetInformationProcess_t  NtSetInformationProcess  = nullptr;
};
const NtApi& GetNtApi();

enum {
    Th_ThreadMmCritical        = 1,
    Th_ThreadPriority         = 2,
    Th_ThreadAffinityMask     = 4,
    Th_ThreadIdealProcessor   = 13,
    Th_ThreadPriorityBoost    = 14,
    Th_ThreadPagePriority     = 17,
    Th_ThreadIoPriority       = 31,
    Th_ThreadPowerThrottling  = 36,
    Th_ThreadNameInformation  = 38,
    Pr_ProcessPriority        = 16,
    Pr_ProcessIoPriority      = 33,
    Pr_ProcessPowerThrottling = 38
};

struct UNICODE_STRING_EX {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
};

struct THREAD_NAME_INFORMATION_EX {
    UNICODE_STRING_EX Name;
};

std::wstring GetThreadName(HANDLE hThread);

#ifndef THREAD_POWER_THROTTLING_EXECUTION_SPEED
#define THREAD_POWER_THROTTLING_EXECUTION_SPEED       0x1
#define THREAD_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x2
#endif

struct THREAD_POWER_THROTTLING_STATE_EX {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
};
#define THREAD_POWER_THROTTLING_CURRENT_VERSION_EX 1

class CpuSample {
public:
    void SampleProcess(HANDLE hProcess, DWORD pid, FILETIME now = {});
    double Percent() const { return m_pct; }
private:
    double m_pct = 0.0;
    ULARGE_INTEGER m_lastKernel = {}, m_lastUser = {};
    ULARGE_INTEGER m_lastNow = {};
    bool m_has = false;
};

}

#endif
