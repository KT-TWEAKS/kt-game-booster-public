#include "powersys.h"
#include "util.h"
#include "detect.h"
#include "procopt.h"
#include "threadopt.h"
#include "snapshot.h"
#include <powrprof.h>
#include <dwmapi.h>
#include <map>

#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "dwmapi.lib")

namespace kt {

static GUID g_savedScheme = GUID_NULL;
static bool g_hasSaved = false;

bool SavePowerState() {
    GUID* scheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS || !scheme)
        return false;
    g_savedScheme = *scheme;
    g_hasSaved = true;
    LocalFree(scheme);
    return true;
}

static const GUID kHighPerformanceScheme = {
    0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};

bool SetHighPerformancePower() {
    return PowerSetActiveScheme(nullptr, &kHighPerformanceScheme) == ERROR_SUCCESS;
}

bool RestorePowerState() {
    if (!g_hasSaved) return false;
    BOOL ok = PowerSetActiveScheme(nullptr, &g_savedScheme) == ERROR_SUCCESS;
    g_hasSaved = false;
    return ok;
}

static const GUID kProcessorSubgroup = {
    0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
static const GUID kProcessorIdleDisable = {
    0x5d76a244, 0x4044, 0x4e05, {0x8c, 0xd3, 0x53, 0x41, 0x7d, 0x5d, 0xe5, 0xe6}};

static DWORD g_oldIdleDisable = 0;
static bool g_cstateApplied = false;
static GUID g_cstateScheme = GUID_NULL;

bool DisableCStates(SessionSnapshot& snapshot) {
    HKEY root = nullptr;
    GUID* scheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS || !scheme)
        return false;
    DWORD ac = 0;
    if (PowerReadACValueIndex(root, scheme, &kProcessorSubgroup,
                              &kProcessorIdleDisable, &ac) != ERROR_SUCCESS ||
        !snapshot.CapturePowerSetting(*scheme, kProcessorSubgroup, kProcessorIdleDisable)) {
        LocalFree(scheme);
        Log(L"cstate", L"C-State change skipped: original value could not be journaled");
        return false;
    }
    g_oldIdleDisable = ac;
    if (PowerWriteACValueIndex(root, scheme, &kProcessorSubgroup,
                               &kProcessorIdleDisable, 1) == ERROR_SUCCESS) {
        PowerSetActiveScheme(nullptr, scheme);
        g_cstateScheme = *scheme;
        g_cstateApplied = true;
        LocalFree(scheme);
        Log(L"cstate", L"C-State idle disabled");
        return true;
    }
    if (scheme) LocalFree(scheme);
    Log(L"cstate", L"PowerWriteACValueIndex failed");
    return false;
}

bool RestoreCStates() {
    if (!g_cstateApplied) return false;
    HKEY root = nullptr;
    if (!IsEqualGUID(g_cstateScheme, GUID_NULL)) {
        PowerWriteACValueIndex(root, &g_cstateScheme, &kProcessorSubgroup,
                               &kProcessorIdleDisable, g_oldIdleDisable);
        PowerSetActiveScheme(nullptr, &g_cstateScheme);
    }
    g_cstateApplied = false;
    g_cstateScheme = GUID_NULL;
    Log(L"cstate", L"C-State idle restored");
    return true;
}

static const GUID kBoostModeGuid = {0xbe337238,0x0d82,0x4146,{0xa9,0x60,0x4f,0x37,0x49,0xd4,0x70,0xc7}};
static const GUID kBoostPolicyGuid = {0x45bcc044,0xd885,0x43e2,{0x86,0x05,0xee,0x0e,0xc6,0xe9,0x6b,0x59}};
static const GUID kCoreParkMinGuid = {0x0cc5b647,0xc1df,0x4637,{0x89,0x1a,0xde,0xc3,0x5c,0x31,0x85,0x83}};
static const GUID kCoreParkMaxGuid = {0xea062031,0x0e34,0x4ff1,{0x9b,0x6d,0xeb,0x10,0x59,0x33,0x40,0x28}};

static struct { bool has; DWORD old; } g_boostMode, g_boostPolicy, g_parkMin, g_parkMax;
static bool g_boostApplied = false;
static bool g_turboApplied = false;
static bool g_parkingApplied = false;
static GUID g_boostScheme = GUID_NULL;

static bool WriteSchemeValue(const GUID& scheme, const GUID* sub, const GUID* key, DWORD value) {
    HKEY root = nullptr;
    bool ok = PowerWriteACValueIndex(root, &scheme, sub, key, value) == ERROR_SUCCESS;
    if (ok) {
        GUID* active = nullptr;
        if (PowerGetActiveScheme(nullptr, &active) == ERROR_SUCCESS && active) {
            if (IsEqualGUID(*active, scheme)) PowerSetActiveScheme(nullptr, &scheme);
            LocalFree(active);
        }
    }
    return ok;
}

static bool ReadSchemeValue(const GUID& scheme, const GUID* sub, const GUID* key, DWORD* out) {
    HKEY root = nullptr;
    return PowerReadACValueIndex(root, &scheme, sub, key, out) == ERROR_SUCCESS;
}

bool ApplyMaxBoost(bool turboBoost, bool coreParkingOff, SessionSnapshot& snapshot) {
    if (g_boostApplied && g_turboApplied == turboBoost && g_parkingApplied == coreParkingOff)
        return true;
    if (g_boostApplied) RestoreMaxBoost();
    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active) return false;
    const GUID scheme = *active;
    LocalFree(active);
    g_boostScheme = scheme;
    auto capture = [&](const GUID& setting, DWORD* old) {
        return ReadSchemeValue(scheme, &kProcessorSubgroup, &setting, old) &&
               snapshot.CapturePowerSetting(scheme, kProcessorSubgroup, setting);
    };
    g_boostMode.has = turboBoost && capture(kBoostModeGuid, &g_boostMode.old);
    g_boostPolicy.has = turboBoost && capture(kBoostPolicyGuid, &g_boostPolicy.old);
    g_parkMin.has = coreParkingOff && capture(kCoreParkMinGuid, &g_parkMin.old);
    g_parkMax.has = coreParkingOff && capture(kCoreParkMaxGuid, &g_parkMax.old);
    bool applied = false;
    if (g_boostMode.has) applied |= WriteSchemeValue(scheme, &kProcessorSubgroup, &kBoostModeGuid, 2);
    if (g_boostPolicy.has) applied |= WriteSchemeValue(scheme, &kProcessorSubgroup, &kBoostPolicyGuid, 100);
    if (g_parkMin.has) applied |= WriteSchemeValue(scheme, &kProcessorSubgroup, &kCoreParkMinGuid, 100);
    if (g_parkMax.has) applied |= WriteSchemeValue(scheme, &kProcessorSubgroup, &kCoreParkMaxGuid, 100);
    if (applied) {
        g_boostApplied = true;
        g_turboApplied = turboBoost;
        g_parkingApplied = coreParkingOff;
        Log(L"boost", L"turbo=%s, core parking=%s", turboBoost ? L"aggressive" : L"unchanged",
            coreParkingOff ? L"off" : L"unchanged");
    }
    return applied;
}

bool RestoreMaxBoost() {
    if (!g_boostApplied) return false;
    if (g_boostMode.has) WriteSchemeValue(g_boostScheme, &kProcessorSubgroup, &kBoostModeGuid, g_boostMode.old);
    if (g_boostPolicy.has) WriteSchemeValue(g_boostScheme, &kProcessorSubgroup, &kBoostPolicyGuid, g_boostPolicy.old);
    if (g_parkMin.has) WriteSchemeValue(g_boostScheme, &kProcessorSubgroup, &kCoreParkMinGuid, g_parkMin.old);
    if (g_parkMax.has) WriteSchemeValue(g_boostScheme, &kProcessorSubgroup, &kCoreParkMaxGuid, g_parkMax.old);
    g_boostApplied = false;
    g_turboApplied = false;
    g_parkingApplied = false;
    g_boostMode.has = g_boostPolicy.has = g_parkMin.has = g_parkMax.has = false;
    g_boostScheme = GUID_NULL;
    Log(L"boost", L"turbo/policy/parking restored");
    return true;
}

bool EnableDwmMmcss() {
    if (DwmEnableMMCSS(TRUE) != S_OK) {
        Log(L"dwm", L"DwmEnableMMCSS failed: 0x%08X", (unsigned)GetLastError());
        return false;
    }
    Log(L"dwm", L"MMCSS enabled");
    return true;
}

void DisableDwmMmcss() {
    DwmEnableMMCSS(FALSE);
    Log(L"dwm", L"MMCSS restored");
}

static std::map<DWORD, ThreadInfo> g_dwmOriginalThreads;

void BoostDwmThreads(SessionSnapshot& snapshot) {
    auto pids = FindPidsByName(L"dwm.exe");
    if (pids.empty()) {
        Log(L"dwm-prio", L"dwm.exe not found");
        return;
    }
    for (DWORD pid : pids) {
        auto threads = EnumerateThreads(pid);
        int boosted = 0;
        for (auto& t : threads) {
            if (!t.handle) continue;
            if (t.priorityKnown &&
                (t.name == L"DWM Master Input Thread" || t.name == L"DWM Kernel Sensor Thread")) {
                if (!snapshot.CaptureThread(t)) continue;
                if (g_dwmOriginalThreads.count(t.tid) == 0) {
                    ThreadInfo original = t;
                    original.handle = nullptr;
                    original.suspendable = false;
                    g_dwmOriginalThreads.emplace(t.tid, std::move(original));
                }
                SetThreadPriority(t.handle, THREAD_PRIORITY_HIGHEST);
                boosted++;
            }
        }
        for (auto& t : threads) if (t.handle) CloseHandle(t.handle);
        if (boosted)
            Log(L"dwm-prio", L"boosted %d DWM thread(s)", boosted);
        else
            Log(L"dwm-prio", L"no DWM target thread found");
    }
}

void RestoreDwmThreads() {
    for (const auto& saved : g_dwmOriginalThreads) {
        HANDLE h = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION |
                              THREAD_SUSPEND_RESUME, FALSE, saved.first);
        if (h) {
            RestoreThreadInfo(h, saved.second);
            CloseHandle(h);
        }
    }
    if (!g_dwmOriginalThreads.empty()) Log(L"dwm-prio", L"DWM thread states restored");
    g_dwmOriginalThreads.clear();
}

static GUID g_savedGameScheme = GUID_NULL;
static bool g_gamePlanApplied = false;

bool ApplyGamingPowerPlan(const GUID& gamePlan) {
    if (g_gamePlanApplied) return true;
    GUID* scheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS || !scheme)
        return false;
    g_savedGameScheme = *scheme;
    LocalFree(scheme);
    if (PowerSetActiveScheme(nullptr, &gamePlan) != ERROR_SUCCESS) {
        Log(L"power", L"gaming plan switch failed");
        return false;
    }
    g_gamePlanApplied = true;
    Log(L"power", L"gaming power plan activated");
    return true;
}

bool RestoreGamingPowerPlan() {
    if (!g_gamePlanApplied) return false;
    BOOL ok = PowerSetActiveScheme(nullptr, &g_savedGameScheme) == ERROR_SUCCESS;
    g_gamePlanApplied = false;
    Log(L"power", L"gaming power plan restored (prev scheme back)");
    return ok;
}

static bool RestartExplorerIfNeeded() {
    if (!FindPidsByName(L"explorer.exe").empty()) {
        Log(L"shell", L"explorer already running - nothing to restore");
        return true;
    }
    wchar_t sys[MAX_PATH] = {0};
    if (GetWindowsDirectoryW(sys, MAX_PATH)) {
        std::wstring explorer = std::wstring(sys) + L"\\explorer.exe";
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(explorer.c_str(), nullptr, nullptr, nullptr, FALSE,
                           0, nullptr, sys, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            Log(L"shell", L"explorer restarted");
            return true;
        } else {
            Log(L"shell", L"explorer restart failed (err %u) - start it manually)", GetLastError());
        }
    }
    return false;
}

bool RevertExtremeShell(const std::vector<DWORD>& winlogonPids, SessionSnapshot& snapshot) {
    bool ok = true;
    for (DWORD pid : winlogonPids) {
        if (!snapshot.RestoreProcessThreads(pid)) {
            ok = false;
            Log(L"shell", L"winlogon threads (PID %u) restore failed", pid);
        } else Log(L"shell", L"winlogon threads (PID %u) restored", pid);
    }
    return RestartExplorerIfNeeded() && ok;
}

bool RecoverExtremeShell() {
    return RestartExplorerIfNeeded();
}

bool GetProcessCpuSets(HANDLE hProcess, std::vector<DWORD>* setIds) {
    if (!hProcess || !setIds) return false;
    setIds->clear();
    typedef BOOL(WINAPI* GetProcessDefaultCpuSets_t)(HANDLE, ULONG*, ULONG, PULONG);
    static GetProcessDefaultCpuSets_t pFn = (GetProcessDefaultCpuSets_t)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetProcessDefaultCpuSets");
    if (!pFn) return false;
    ULONG needed = 0;
    if (!pFn(hProcess, nullptr, 0, &needed) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return needed == 0;
    if (!needed) return true;
    setIds->resize(needed);
    return pFn(hProcess, setIds->data(), needed, &needed) != FALSE;
}

bool SetProcessCpuSets(HANDLE hProcess, const std::vector<DWORD>& setIds) {
    if (!hProcess) return false;
    typedef BOOL(WINAPI* SetProcessDefaultCpuSets_t)(HANDLE, ULONG*, ULONG);
    static SetProcessDefaultCpuSets_t pFn = (SetProcessDefaultCpuSets_t)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessDefaultCpuSets");
    if (!pFn) return false;
    ULONG* data = setIds.empty() ? nullptr : const_cast<ULONG*>(reinterpret_cast<const ULONG*>(setIds.data()));
    return pFn(hProcess, data, (ULONG)setIds.size()) != FALSE;
}

namespace {
struct SavedRegValue {
    bool existed = false;
    bool supported = true;
    DWORD value = 0;
};

SavedRegValue ReadDwordUser(const wchar_t* keyPath, const wchar_t* valueName) {
    SavedRegValue s;
    DWORD type = 0, size = sizeof(DWORD);
    HKEY h = nullptr;
    LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &h);
    if (openResult == ERROR_SUCCESS) {
        LONG readResult = RegQueryValueExW(h, valueName, nullptr, &type, (LPBYTE)&s.value, &size);
        if (readResult == ERROR_SUCCESS) {
            if (type == REG_DWORD && size == sizeof(DWORD)) s.existed = true;
            else s.supported = false;
        } else if (readResult != ERROR_FILE_NOT_FOUND) {
            s.supported = false;
        }
        RegCloseKey(h);
    } else if (openResult != ERROR_FILE_NOT_FOUND) {
        s.supported = false;
    }
    return s;
}

bool WriteDwordUser(const wchar_t* keyPath, const wchar_t* valueName, DWORD v) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &h, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(h, valueName, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

void DeleteUserValue(const wchar_t* keyPath, const wchar_t* valueName) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
        RegDeleteValueW(h, valueName);
        RegCloseKey(h);
    }
}

SavedRegValue g_savedDvrEnabled;
SavedRegValue g_savedAppCapture;
bool g_dvrSaved = false;
bool g_dvrEnabledChanged = false;
bool g_appCaptureChanged = false;

const wchar_t kGameConfigStoreKey[] = L"System\\GameConfigStore";
const wchar_t kGameDvrKey[]         = L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR";
}

bool DisableGameDvr(SessionSnapshot& snapshot) {
    if (g_dvrSaved) return true;
    bool changed = false;

    SavedRegValue prev = ReadDwordUser(kGameConfigStoreKey, L"GameDVR_Enabled");
    if (!prev.supported) {
        Log(L"gamedvr", L"GameDVR_Enabled has an unsupported registry type or cannot be read; skipped");
        return false;
    }
    if (!prev.existed || prev.value != 0) {
        if (!snapshot.CaptureRegistryValue(1, L"", prev.existed,
                                           std::to_wstring(prev.value))) {
            Log(L"gamedvr", L"journal unavailable; Game DVR tweak skipped");
            return false;
        }
        if (WriteDwordUser(kGameConfigStoreKey, L"GameDVR_Enabled", 0)) {
            g_savedDvrEnabled = prev;
            g_dvrEnabledChanged = true;
            changed = true;
        }
    }

    prev = ReadDwordUser(kGameDvrKey, L"AppCaptureEnabled");
    if (!prev.supported) {
        Log(L"gamedvr", L"AppCaptureEnabled has an unsupported registry type or cannot be read; skipped");
        if (changed) g_dvrSaved = true;
        return changed;
    }
    if (!prev.existed || prev.value != 0) {
        if (!snapshot.CaptureRegistryValue(2, L"", prev.existed,
                                           std::to_wstring(prev.value))) {
            Log(L"gamedvr", L"journal unavailable; AppCaptureEnabled tweak skipped");
            if (changed) g_dvrSaved = true;
            return changed;
        }
        if (WriteDwordUser(kGameDvrKey, L"AppCaptureEnabled", 0)) {
            g_savedAppCapture = prev;
            g_appCaptureChanged = true;
            changed = true;
        }
    }

    if (changed) {
        g_dvrSaved = true;
        Log(L"gamedvr", L"Game DVR capture disabled for the session");
        return true;
    }
    return false;
}

void RestoreGameDvr() {
    if (!g_dvrSaved) return;
    if (g_dvrEnabledChanged) {
        if (g_savedDvrEnabled.existed)
            WriteDwordUser(kGameConfigStoreKey, L"GameDVR_Enabled", g_savedDvrEnabled.value);
        else
            DeleteUserValue(kGameConfigStoreKey, L"GameDVR_Enabled");
    }
    if (g_appCaptureChanged) {
        if (g_savedAppCapture.existed)
            WriteDwordUser(kGameDvrKey, L"AppCaptureEnabled", g_savedAppCapture.value);
        else
            DeleteUserValue(kGameDvrKey, L"AppCaptureEnabled");
    }
    g_dvrSaved = false;
    g_dvrEnabledChanged = false;
    g_appCaptureChanged = false;
    Log(L"gamedvr", L"Game DVR settings restored");
}

}
