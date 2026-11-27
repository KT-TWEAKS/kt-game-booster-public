#include "util.h"
#include <stdarg.h>
#include <timeapi.h>
#include <objbase.h>

namespace kt {

static LogSinkFn g_logSink = nullptr;

void SetLogSink(LogSinkFn fn) {
    g_logSink = fn;
}

void Log(const wchar_t* module, const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, 1024, _TRUNCATE, fmt, ap);
    va_end(ap);
    std::wstring line = L"[";
    line += module;
    line += L"] ";
    line += buf;
    line += L"\n";
    if (g_logSink) g_logSink(line.c_str());
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut && hOut != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        if (WriteConsoleW(hOut, line.c_str(), (DWORD)line.size(), &written, nullptr))
            return;
    }
    fputws(line.c_str(), stdout);
    fflush(stdout);
}

bool EnableDebugPrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;
    LUID luid;
    bool ok = LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid);
    if (ok) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ok = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
        ok = ok && (GetLastError() == ERROR_SUCCESS);
    }
    CloseHandle(hToken);
    return ok;
}

HANDLE AcquireSingleInstance() {
    HANDLE h = CreateMutexW(nullptr, TRUE, L"Local\\KTGameBooster");
    if (!h) return nullptr;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        return nullptr;
    }
    return h;
}

void SetTimerResolution(bool on) {
    static bool inited = false;
    static UINT minPeriod = 0;
    if (!inited) {
        TIMECAPS caps;
        if (timeGetDevCaps(&caps, sizeof(caps)) == TIMERR_NOERROR)
            minPeriod = caps.wPeriodMin;
        inited = true;
    }
    if (on)
        timeBeginPeriod(minPeriod ? minPeriod : 1);
    else
        timeEndPeriod(minPeriod ? minPeriod : 1);
}

bool GuidFromString(const wchar_t* s, GUID* out) {
    if (!s || !out) return false;
    wchar_t hex[40] = {};
    int n = 0;
    for (const wchar_t* p = s; *p && n < 40; p++) {
        wchar_t c = *p;
        if (c == L'-' || c == L'{' || c == L'}') continue;
        if (!iswxdigit((wint_t)c)) return false;
        hex[n++] = c;
    }
    if (n != 32) return false;
    hex[32] = 0;
    wchar_t dashed[40] = {};
    int src[5] = {0, 8, 12, 16, 20};
    int len[5] = {8, 4, 4, 4, 12};
    int pos = 0;
    for (int i = 0; i < 5; i++) {
        if (i) dashed[pos++] = L'-';
        wcsncpy_s(dashed + pos, len[i] + 1, hex + src[i], len[i]);
        pos += len[i];
    }
    return SUCCEEDED(CLSIDFromString(dashed, out));
}

bool WildMatch(const wchar_t* pattern, const wchar_t* text) {
    const wchar_t* p = pattern;
    const wchar_t* t = text;
    const wchar_t* star = nullptr;
    const wchar_t* marker = nullptr;
    while (*t) {
        if (*p == L'*') {
            star = p++;
            marker = t;
        } else if (towlower(*p) == towlower(*t)) {
            p++; t++;
        } else if (star) {
            p = star + 1;
            t = ++marker;
        } else {
            return false;
        }
    }
    while (*p == L'*') p++;
    return *p == 0;
}

bool GetProcessModuleBaseNameSafe(DWORD pid, wchar_t* out, size_t len) {
    if (out && len) out[0] = 0;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    bool ok = false;
    DWORD dwLen = (DWORD)len;
    if (QueryFullProcessImageNameW(h, 0, out, &dwLen)) {
        len = dwLen;
        wchar_t* slash = wcsrchr(out, L'\\');
        if (slash) {
            size_t rest = len - (size_t)(slash - out) - 1;
            memmove(out, slash + 1, rest * sizeof(wchar_t));
            out[rest] = 0;
        }
        ok = true;
    } else {
        DWORD sz = (DWORD)len;
        if (GetModuleBaseNameW(h, nullptr, out, sz)) ok = true;
    }
    CloseHandle(h);
    return ok;
}

std::wstring GetProcessNameById(DWORD pid) {
    wchar_t buf[512];
    if (!GetProcessModuleBaseNameSafe(pid, buf, 512)) return L"";
    return WLower(buf);
}

std::wstring GetProcessNameByHandle(HANDLE h) {
    wchar_t buf[512] = {0};
    DWORD sz = 512;
    if (GetModuleBaseNameW(h, nullptr, buf, sz)) return WLower(buf);
    return L"";
}

std::wstring GetProcPathByHandle(HANDLE h) {
    wchar_t buf[1024] = {0};
    DWORD sz = 1024;
    if (QueryFullProcessImageNameW(h, 0, buf, &sz)) return WLower(buf);
    return L"";
}

bool IsProcessRunning(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD code = 0;
    bool running = (GetExitCodeProcess(h, &code) && code == STILL_ACTIVE);
    CloseHandle(h);
    return running;
}

const NtApi& GetNtApi() {
    static NtApi api;
    static bool inited = false;
    if (!inited) {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            api.NtQueryInformationThread = (NtQueryInformationThread_t)GetProcAddress(ntdll, "NtQueryInformationThread");
            api.NtSetInformationThread   = (NtSetInformationThread_t)GetProcAddress(ntdll, "NtSetInformationThread");
            api.NtSuspendThread          = (NtSuspendThread_t)GetProcAddress(ntdll, "NtSuspendThread");
            api.NtResumeThread           = (NtResumeThread_t)GetProcAddress(ntdll, "NtResumeThread");
            api.NtSuspendProcess         = (NtSuspendProcess_t)GetProcAddress(ntdll, "NtSuspendProcess");
            api.NtResumeProcess          = (NtResumeProcess_t)GetProcAddress(ntdll, "NtResumeProcess");
            api.NtQueryInformationProcess = (NtQueryInformationProcess_t)GetProcAddress(ntdll, "NtQueryInformationProcess");
            api.NtSetInformationProcess  = (NtSetInformationProcess_t)GetProcAddress(ntdll, "NtSetInformationProcess");
        }
        inited = true;
    }
    return api;
}

std::wstring GetThreadName(HANDLE hThread) {
    const NtApi& nt = GetNtApi();
    if (!nt.NtQueryInformationThread) return L"";
    static thread_local wchar_t buffer[512];
    buffer[0] = 0;
    THREAD_NAME_INFORMATION_EX info;
    UNICODE_STRING_EX* us = &info.Name;
    ULONG outLen = 0;
    LONG st = nt.NtQueryInformationThread(hThread, Th_ThreadNameInformation, &info, (ULONG)sizeof(info), &outLen);
    if (st == 0 && us->Buffer && us->Length > 0) {
        size_t len = us->Length / sizeof(wchar_t);
        if (len >= 512) len = 511;
        memcpy(buffer, us->Buffer, len * sizeof(wchar_t));
        buffer[len] = 0;
        return buffer;
    }
    return L"";
}

void CpuSample::SampleProcess(HANDLE hProcess, DWORD pid, FILETIME now) {
    FILETIME create, exit, kernel, user;
    if (!GetProcessTimes(hProcess, &create, &exit, &kernel, &user)) {
        m_pct = 0.0;
        return;
    }
    ULARGE_INTEGER k = { kernel.dwLowDateTime, kernel.dwHighDateTime };
    ULARGE_INTEGER u = { user.dwLowDateTime, user.dwHighDateTime };
    ULARGE_INTEGER n = { now.dwLowDateTime, now.dwHighDateTime };
    if (m_has) {
        ULONGLONG dt = (n.QuadPart - m_lastNow.QuadPart) ? (n.QuadPart - m_lastNow.QuadPart) : 1;
        ULONGLONG busy = (k.QuadPart - m_lastKernel.QuadPart) + (u.QuadPart - m_lastUser.QuadPart);
        m_pct = (double)busy * 100.0 / (double)dt;
        if (m_pct < 0.0) m_pct = 0.0;
        if (m_pct > 100.0 * 256.0) m_pct = 100.0 * 256.0;
    }
    m_lastKernel = k;
    m_lastUser = u;
    m_lastNow = n;
    m_has = true;
}

}
