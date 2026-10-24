#include "ktengine_api.h"
#include "booster.h"
#include "config.h"
#include "util.h"
#include "winsvcmon.h"
#include "metrics.h"
#include "procopt.h"
#include "gpumon.h"

#include <cstring>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <algorithm>
#include <fstream>

using namespace kt;

namespace {

BoosterCore*            g_core     = nullptr;
Config                  g_hostCfg;
std::mutex              g_mtx;
std::mutex              g_cfgPathMtx;
std::wstring            g_cfgOverridePath;

struct LogEntry { std::wstring line; int level; };
const size_t            kLogCap = 800;
std::vector<LogEntry>   g_logRing;
std::mutex              g_logMtx;

int LevelFromModule(const wchar_t* mod) {
    if (!mod) return 0;
    std::wstring m(mod);
    const auto close = m.find(L']');
    if (close != std::wstring::npos) m.resize(close);
    for (auto& c : m) if (c >= L'A' && c <= L'Z') c += 32;
    if (m == L"error" || m == L"fail") return 2;
    if (m == L"warn" || m == L"warning") return 1;
    return 0;
}

void HostLogSink(const wchar_t* line) {
    std::lock_guard<std::mutex> lk(g_logMtx);
    int lvl = 0;
    if (line && line[0] == L'[') {
        const wchar_t* close = wcschr(line, L']');
        if (close) lvl = LevelFromModule(line + 1);
    }
    g_logRing.push_back({ std::wstring(line ? line : L""), lvl });
    if (g_logRing.size() > kLogCap) g_logRing.erase(g_logRing.begin());
}

FILETIME g_prevIdle = {0}, g_prevKern = {0}, g_prevUser = {0};
bool     g_prevValid = false;
ULONGLONG g_sysRamUsedMB = 0, g_sysRamTotalMB = 0;
double      g_lastSysCpuPct = 0.0;
std::atomic<int> g_lastGpuPctX100{-1};
int      g_sysProcCount = 0;
ULONGLONG g_lastProcCountTick = 0;
MetricsHistory g_metrics;

Config LoadCfgOrOverride() {
    std::wstring overridePath;
    {
        std::lock_guard<std::mutex> lk(g_cfgPathMtx);
        overridePath = g_cfgOverridePath;
    }
    if (!overridePath.empty()) return LoadConfigFrom(overridePath);
    return LoadConfig();
}

bool IsGovernorProtectedName(DWORD pid, const std::wstring& processName) {
    if (pid < 8 || pid == GetCurrentProcessId()) return true;
    const std::wstring name = WLower(processName);
    static const wchar_t* protectedNames[] = {
        L"system", L"registry", L"smss.exe", L"csrss.exe", L"wininit.exe",
        L"winlogon.exe", L"services.exe", L"lsass.exe", L"svchost.exe",
        L"fontdrvhost.exe", L"dwm.exe", L"memory compression"
    };
    for (const auto* item : protectedNames)
        if (name == item) return true;
    return name.empty();
}

int GovernProcessImpl(DWORD pid, ULONGLONG expectedCreationTime,
                      int action, ULONGLONG value) {
    if (!pid || action < 1 || action > 8) return -2;

    const DWORD access = PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION |
                         PROCESS_SET_QUOTA | PROCESS_SUSPEND_RESUME;
    HANDLE h = OpenProcess(access, FALSE, pid);
    if (!h) h = OpenProcessForOpt(pid);
    if (!h) {
        Log(L"governor", L"OpenProcess falhou para PID %lu (erro %lu)", pid, GetLastError());
        return 0;
    }

    FILETIME created{}, exited{}, kernel{}, user{};
    if (expectedCreationTime != 0) {
        const ULONGLONG actual = GetProcessTimes(h, &created, &exited, &kernel, &user)
            ? (((ULONGLONG)created.dwHighDateTime << 32) | created.dwLowDateTime) : 0;
        if (actual != expectedCreationTime) {
            CloseHandle(h);
            Log(L"governor", L"stale process row rejected for PID %lu", pid);
            return -3;
        }
    }

    const std::wstring processName = GetProcessNameByHandle(h);
    if (IsGovernorProtectedName(pid, processName)) {
        CloseHandle(h);
        Log(L"governor", L"ação bloqueada para processo protegido PID %lu", pid);
        return -1;
    }

    bool ok = false;
    switch (action) {
        case 1: {
            if (value > 4) break;
            const PriorityClass classes[] = { PriorityClass::Idle, PriorityClass::BelowNormal,
                PriorityClass::Normal, PriorityClass::AboveNormal, PriorityClass::High };
            ok = SetProcPriorityClass(h, classes[(size_t)value]);
            break;
        }
        case 3:
            ok = value <= 1 && SetProcEcoQoS(h, value != 0);
            break;
        case 4: {
            const int io[] = { 0, 1, 2, 3 };
            ok = value < 4 && SetProcIoPriority(h, io[(size_t)value]);
            break;
        }
        case 5: {
            const int mem[] = { 1, 3, 5 };
            ok = value < 3 && SetProcMemoryPriority(h, mem[(size_t)value]);
            break;
        }
        case 6:
            ok = value <= 1 && (value ? SuspendProcess(h) : ResumeProcess(h));
            break;
        case 7:
            ok = TrimWorkingSet(h);
            break;
        case 8:
            ok = value <= 1 && SetProcessPriorityBoost(h, value ? FALSE : TRUE) != FALSE;
            break;
    }

    CloseHandle(h);
    Log(L"governor", L"%s PID %lu · ação %d · valor %llu · %s",
        processName.c_str(), pid, action, value, ok ? L"ok" : L"falhou");
    return ok ? 1 : 0;
}

void SampleSystemMetrics() {
    FILETIME idle, kern, user;
    if (!GetSystemTimes(&idle, &kern, &user)) return;

    double sysCpuPct = 0.0;
    if (g_prevValid) {
        ULONGLONG i0 = ((ULONGLONG)g_prevIdle.dwHighDateTime << 32) | g_prevIdle.dwLowDateTime;
        ULONGLONG k0 = ((ULONGLONG)g_prevKern.dwHighDateTime << 32) | g_prevKern.dwLowDateTime;
        ULONGLONG u0 = ((ULONGLONG)g_prevUser.dwHighDateTime << 32) | g_prevUser.dwLowDateTime;
        ULONGLONG i1 = ((ULONGLONG)idle.dwHighDateTime << 32) | idle.dwLowDateTime;
        ULONGLONG k1 = ((ULONGLONG)kern.dwHighDateTime << 32) | kern.dwLowDateTime;
        ULONGLONG u1 = ((ULONGLONG)user.dwHighDateTime << 32) | user.dwLowDateTime;
        ULONGLONG dk = k1 - k0, du = u1 - u0, di = i1 - i0;
        ULONGLONG total = dk + du;
        if (total > 0) sysCpuPct = (double)(total - di) / (double)total * 100.0;
    }
    g_prevIdle = idle; g_prevKern = kern; g_prevUser = user; g_prevValid = true;
    g_lastSysCpuPct = sysCpuPct;

    MEMORYSTATUSEX msx = { sizeof(msx) };
    if (GlobalMemoryStatusEx(&msx)) {
        g_sysRamTotalMB = msx.ullTotalPhys / (1024 * 1024);
        g_sysRamUsedMB  = (msx.ullTotalPhys - msx.ullAvailPhys) / (1024 * 1024);
    }

    const double gpuPct = GpuMon_TotalPct();
    g_lastGpuPctX100 = (gpuPct >= 0.0) ? (int)(gpuPct * 100.0 + 0.5) : -1;

    const ULONGLONG now = GetTickCount64();
    if (!g_lastProcCountTick || now - g_lastProcCountTick >= 2000) {
        int n = 0;
        DWORD procs[4096], cnt = 0, needed = 0;
        if (EnumProcesses(procs, sizeof(procs), &needed)) {
            cnt = needed / sizeof(DWORD);
            for (DWORD i = 0; i < cnt; i++) {
                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, procs[i]);
                if (!h) continue;
                FILETIME c, e, k, u;
                if (GetProcessTimes(h, &c, &e, &k, &u)) n++;
                CloseHandle(h);
            }
            g_sysProcCount = n;
        }
        g_lastProcCountTick = now;
    }

    MetricsSample ms;
    ms.cpuPct     = sysCpuPct;
    ms.ramUsedMB  = g_sysRamUsedMB;
    ms.ramTotalMB = g_sysRamTotalMB;
    ms.fps        = g_core ? (DWORD)((g_core->State().fpsX100.load() + 50) / 100) : 0;
    ms.pingMs     = 0;
    ms.gpuPct     = gpuPct;
    ms.tickMs     = GetTickCount64();
    g_metrics.Push(ms);

    if (g_core) {
        DWORD cpuX100 = (DWORD)(sysCpuPct * 100.0);
        DWORD cur = g_core->State().peakCpuPct.load();
        if (cpuX100 > cur) g_core->State().peakCpuPct.store(cpuX100);
        DWORD ramX100 = (g_sysRamTotalMB > 0)
            ? (DWORD)((double)g_sysRamUsedMB * 10000.0 / (double)g_sysRamTotalMB) : 0;
        cur = g_core->State().peakRamPct.load();
        if (ramX100 > cur) g_core->State().peakRamPct.store(ramX100);
    }
}

bool SetFeatureByName(Config& cfg, const std::string& name, bool v) {
    Features& f = cfg.features;
    if (name == "dwm_mmcss")                  { f.dwm_mmcss = v; return true; }
    if (name == "dwm_thread_priority")        { f.dwm_thread_priority = v; return true; }
    if (name == "gpu_priority")               { f.gpu_priority = v; return true; }
    if (name == "background_limiter")         { f.background_limiter = v; return true; }
    if (name == "thread_power_background_throttle") { f.thread_power_background_throttle = v; return true; }
    if (name == "thread_power_game_unthrottle")     { f.thread_power_game_unthrottle = v; return true; }
    if (name == "io_priority_background")     { f.io_priority_background = v; return true; }
    if (name == "io_priority_game_high")      { f.io_priority_game_high = v; return true; }
    if (name == "eco_qos_background")         { f.eco_qos_background = v; return true; }
    if (name == "suspend_gameinput_threads")  { f.suspend_gameinput_threads = v; return true; }
    if (name == "disable_c_states")           { f.disable_c_states = v; return true; }
    if (name == "disable_priority_boost")     { f.disable_priority_boost = v; return true; }
    if (name == "background_cpu_pin")         { f.background_cpu_pin = v; return true; }
    if (name == "boost_input_threads")        { f.boost_input_threads = v; return true; }
    if (name == "high_performance_plan")      { f.high_performance_plan = v; return true; }
    if (name == "turbo_boost")                { f.turbo_boost = v; return true; }
    if (name == "core_parking_disable")       { f.core_parking_disable = v; return true; }
    if (name == "mmcss_game")                 { f.mmcss_game = v; return true; }
    if (name == "page_priority_game")         { f.page_priority_game = v; return true; }
    if (name == "memory_priority_background") { f.memory_priority_background = v; return true; }
    if (name == "memory_priority_game_high")  { f.memory_priority_game_high = v; return true; }
    if (name == "trim_background_ws")         { f.trim_background_ws = v; return true; }
    if (name == "tournament_mode")            { f.tournament_mode = v; return true; }
    if (name == "memory_compression_disable") { f.memory_compression_disable = v; return true; }
    if (name == "gpu_hardware_scheduling")    { f.gpu_hardware_scheduling = v; return true; }
    if (name == "auto_profile_switching")     { f.auto_profile_switching = v; return true; }
    if (name == "gaming_power_plans")         { f.gaming_power_plans = v; return true; }
    if (name == "game_tree_boost")            { f.game_tree_boost = v; return true; }
    if (name == "pause_services_gaming")      { f.pause_services_gaming = v; return true; }
    if (name == "game_dvr_disable")           { f.game_dvr_disable = v; return true; }
    if (name == "fps_monitor")                { f.fps_monitor = v; return true; }
    if (name == "anticheat_safe_mode")        { f.anticheat_safe_mode = v; return true; }
    return false;
}

}

extern "C" {

KT_API int KT_Initialize() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_core) return 1;
    g_core = new BoosterCore();
    SetLogSink(HostLogSink);
    g_hostCfg = LoadCfgOrOverride();
    g_core->SetConfigSource([]() { return LoadCfgOrOverride(); });
    g_prevValid = false;
    g_lastSysCpuPct = 0.0;
    g_sysRamUsedMB = g_sysRamTotalMB = 0;
    g_sysProcCount = 0;
    g_lastProcCountTick = 0;
    g_metrics.Clear();
    g_lastGpuPctX100 = -1;
    if (!GpuMon_Init())
        Log(L"gpumon", L"PDH GPU Engine counters unavailable (telemetry off)");
    return 0;
}

KT_API void KT_Shutdown() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_core) {
        g_core->Stop();
        delete g_core;
        g_core = nullptr;
    }
    SetLogSink(nullptr);
    GpuMon_Shutdown();
}

KT_API int KT_Start() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_core) return 0;
    if (g_core->IsRunning()) return 1;
    BoosterOptions opt;
    opt.dry_run = false;
    opt.poll_interval_ms = g_hostCfg.poll_interval_ms;
    g_core->Start(g_hostCfg, opt);
    return g_core->IsRunning() ? 1 : 0;
}

KT_API void KT_Stop() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_core) g_core->Stop();
}

KT_API void KT_SetSafeMode(int enabled) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_core) g_core->SetSafeMode(enabled != 0);
}

KT_API int KT_RecoverPendingSession() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_core && !g_core->IsRunning() && g_core->RecoverPendingSession() ? 1 : 0;
}

KT_API void KT_SetPaused(int paused) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_core) g_core->SetPaused(paused != 0);
}

KT_API void KT_SetTournament(int on) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_core) g_core->SetTournament(on != 0);
}

KT_API void KT_SetTestTarget(const wchar_t* exe) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_core) g_core->SetTestTarget(exe ? exe : L"");
}

KT_API int KT_SetFeature(const char* name, int value) {
    if (!name) return 0;
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_core) return 0;
    if (!g_core->IsRunning()) {
        return SetFeatureByName(g_hostCfg, name, value != 0) ? 1 : 0;
    }
    g_hostCfg = g_core->GetConfigSnapshot();
    if (!SetFeatureByName(g_hostCfg, name, value != 0)) return 0;
    g_core->UpdateConfig(g_hostCfg);
    return 1;
}

KT_API int KT_IsElevated() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return 0;
    TOKEN_ELEVATION te{};
    DWORD cb = sizeof(te);
    BOOL ok = GetTokenInformation(hToken, TokenElevation, &te, cb, &cb);
    CloseHandle(hToken);
    return (ok && te.TokenIsElevated) ? 1 : 0;
}

KT_API void KT_RestoreShell() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_core) g_core->RestoreShellNow();
}

KT_API void KT_GetState(KtState* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_core) return;
    SampleSystemMetrics();
    const BoosterState& s = g_core->State();
    out->running         = g_core->IsRunning() ? 1 : 0;
    out->paused          = g_core->IsPaused() ? 1 : 0;
    out->gameActive      = s.gameActive.load() ? 1 : 0;
    out->gamePid         = s.gamePid.load();
    {
        std::lock_guard<std::mutex> nameLock(s.gameNameMutex);
        wcsncpy_s(out->gameName, 64, s.gameName, _TRUNCATE);
    }
    out->bgCount         = s.bgCount.load();
    out->suspendedThreads= s.suspendedThreads.load();
    out->memFreed        = s.memFreed.load();
    out->etwEvents       = s.etwEvents.load();
    out->totalRuns       = s.totalRuns.load();
    out->totalGameSessions = s.totalGameSessions.load();
    out->totalGameSeconds  = s.totalGameSeconds.load();
    ULONGLONG started = s.startedTick.load();
    out->uptimeSec     = started ? (DWORD)((GetTickCount64() - started) / 1000) : 0;
    ULONGLONG gs = s.gameStartedTick.load();
    out->gameUptimeSec = (gs && s.gameActive.load()) ? (DWORD)((GetTickCount64() - gs) / 1000) : 0;
    Config liveCfg = g_core->GetConfigSnapshot();
    out->tournament    = liveCfg.features.tournament_mode ? 1 : 0;
    out->sysCpu = g_lastSysCpuPct;
    out->sysRamUsedMB  = g_sysRamUsedMB;
    out->sysRamTotalMB = g_sysRamTotalMB;
    out->procCount     = g_sysProcCount;
    out->detectionConfidence = s.detectionConfidence.load();
    out->extremeActive = s.extremeActive.load() ? 1 : 0;

    const Features& f = liveCfg.features;
    out->featureMask =
        (f.dwm_mmcss                        ? 0x00000001 : 0) |
        (f.gpu_priority                     ? 0x00000002 : 0) |
        (f.background_limiter               ? 0x00000004 : 0) |
        (f.thread_power_game_unthrottle     ? 0x00000008 : 0) |
        (f.io_priority_background           ? 0x00000010 : 0) |
        (f.eco_qos_background               ? 0x00000020 : 0) |
        (f.suspend_gameinput_threads        ? 0x00000040 : 0) |
        (f.disable_c_states                 ? 0x00000080 : 0) |
        (f.boost_input_threads              ? 0x00000100 : 0) |
        (f.high_performance_plan            ? 0x00000200 : 0) |
        (f.turbo_boost                      ? 0x00000400 : 0) |
        (f.core_parking_disable             ? 0x00000800 : 0) |
        (f.pin_perf_cores                   ? 0x00001000 : 0) |
        (f.trim_background_ws               ? 0x00002000 : 0) |
        (f.network_throttling_disable       ? 0x00004000 : 0) |
        (f.memory_compression_disable       ? 0x00008000 : 0) |
        (f.gpu_hardware_scheduling          ? 0x00010000 : 0) |
        (f.auto_profile_switching           ? 0x00020000 : 0) |
        (f.timer_resolution_500us           ? 0x00040000 : 0) |
        (f.cpu_sets_pin                     ? 0x00080000 : 0) |
        (f.gaming_power_plans               ? 0x00100000 : 0) |
        (f.memory_lock                      ? 0x00200000 : 0) |
        (f.extreme_shell_mode               ? 0x00400000 : 0) |
        (f.extreme_kill_dwm                 ? 0x00800000 : 0) |
        (f.auto_clean_ram                   ? 0x01000000 : 0) |
        (f.game_tree_boost                  ? 0x02000000 : 0) |
        (f.pause_services_gaming            ? 0x04000000 : 0) |
        (f.dwm_thread_priority              ? 0x08000000 : 0) |
        (f.thread_power_background_throttle ? 0x10000000 : 0) |
        (f.io_priority_game_high            ? 0x20000000 : 0) |
        (f.mmcss_game                       ? 0x40000000 : 0) |
        (f.page_priority_game               ? (int)0x80000000u : 0);

    out->autoCleanCount = (int)s.autoCleanCount.load();
    out->servicePauseCount = (int)s.servicePauseCount.load();
    out->boostedChildCount = (int)s.boostedChildCount.load();
    out->peakCpuPctX100    = (int)s.peakCpuPct.load();
    out->peakRamPctX100    = (int)s.peakRamPct.load();
    out->elevated          = KT_IsElevated();
    out->etwActive         = g_core->IsEtwActive() ? 1 : 0;

    out->fpsX100      = (int)s.fpsX100.load();
    out->frameP95us   = (int)s.frameP95Us.load();
    out->frameP99us   = (int)s.frameP99Us.load();
    out->frameSamples = (int)s.frameSamples.load();
    out->featureMask2 =
        (f.game_dvr_disable      ? 0x00000001 : 0) |
        (f.fso_disable           ? 0x00000002 : 0) |
        (f.fps_monitor           ? 0x00000004 : 0) |
        (f.anticheat_safe_mode   ? 0x00000008 : 0);

    out->anticheatSafe = s.anticheatSafe.load() ? 1 : 0;
    out->anticheatKind = s.anticheatKind.load();

    {
        std::lock_guard<std::mutex> profLock(s.gameProfileMutex);
        wcsncpy_s(out->gameProfile, 32, s.gameProfile, _TRUNCATE);
    }

    out->gpuPctX100 = g_lastGpuPctX100.load();
    GpuMon_AdapterName(out->gpuName, 64);
}

KT_API int KT_GetProcesses(KtProc* out, int maxCount) {
    if (!out || maxCount <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_core) return 0;
    std::vector<ActiveProcInfo> act;
    g_core->GetActiveProcessList(act);
    int n = (int)act.size();
    int w = (n < maxCount) ? n : maxCount;
    for (int i = 0; i < w; i++) {
        const ActiveProcInfo& a = act[i];
        KtProc& p = out[i];
        memset(&p, 0, sizeof(p));
        p.pid = a.pid;
        p.isGame = a.isGame ? 1 : 0;
        p.throttled = a.throttled ? 1 : 0;
        wcsncpy_s(p.klass, 32, a.klass.c_str(), _TRUNCATE);
        wcsncpy_s(p.name, 64, a.name.c_str(), _TRUNCATE);
        p.affinity = (ULONGLONG)a.affinity;
    }
    return n;
}

KT_API int KT_GetSysProcesses(KtSysProc* out, int maxCount) {
    if (!out || maxCount <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_core) return 0;
    std::vector<SysProcInfo> list;
    g_core->GetSysProcessList(list);
    int w = (int)((list.size() < (size_t)maxCount) ? list.size() : (size_t)maxCount);
    for (int i = 0; i < w; i++) {
        const SysProcInfo& s = list[i];
        KtSysProc& p = out[i];
        memset(&p, 0, sizeof(p));
        p.pid = s.pid;
        p.isGame = s.isGame ? 1 : 0;
        p.throttled = s.throttled ? 1 : 0;
        p.gameChild = s.gameChild ? 1 : 0;
        p.cpuPct = s.cpuPct;
        p.ramMB = s.ramMB;
        p.affinity = (ULONGLONG)s.affinity;
        p.priority = (int)s.priority;
        wcsncpy_s(p.name, 64, s.name.c_str(), _TRUNCATE);
        wcsncpy_s(p.klass, 32, s.klass.c_str(), _TRUNCATE);
        p.creationTime = s.creationTime;
        double gpuPct = -1.0;
        GpuMon_ProcessPct(s.pid, &gpuPct);
        p.gpuPct = gpuPct;
    }
    return w;
}

KT_API int KT_GovernProcess(DWORD pid, int action, ULONGLONG value) {
    return GovernProcessImpl(pid, 0, action, value);
}

KT_API int KT_GovernProcessEx(DWORD pid, ULONGLONG expectedCreationTime,
                              int action, ULONGLONG value) {
    if (!expectedCreationTime) return -3;
    return GovernProcessImpl(pid, expectedCreationTime, action, value);
}

KT_API int KT_GetServices(KtService* out, int maxCount) {
    if (!out || maxCount <= 0) return 0;
    std::vector<ServiceInfo> list;
    if (EnumServices(list) <= 0) return 0;
    int w = (int)((list.size() < (size_t)maxCount) ? list.size() : (size_t)maxCount);
    for (int i = 0; i < w; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        wcsncpy_s(out[i].name, 64, list[i].name, _TRUNCATE);
        wcsncpy_s(out[i].displayName, 96, list[i].displayName, _TRUNCATE);
        out[i].state = list[i].state;
        out[i].pid = list[i].pid;
        out[i].canStop = list[i].canStop;
        out[i].canPause = list[i].canPause;
        out[i].gamingTarget = list[i].gamingTarget;
        out[i].protectedService = list[i].protectedService;
    }
    return w;
}

KT_API int KT_ServiceControl(const wchar_t* name, int action) {
    if (!name || !*name) return 0;
    std::wstring n = name;
    if (IsProtectedService(n)) {
        Log(L"service", L"blocked control of protected service: %s", name);
        return 0;
    }
    bool ok = false;
    switch (action) {
        case 1: ok = ServiceStart(n); break;
        case 2: ok = ServiceStop(n);  break;
        case 3: ok = ServicePause(n); break;
        case 4: ok = ServiceResume(n);break;
        default: return 0;
    }
    if (ok) {
        static const wchar_t* labels[] = { L"?", L"started", L"stopped", L"paused", L"resumed" };
        Log(L"service", L"%s: service %s", labels[action], name);
    }
    return ok ? 1 : 0;
}

static void RewriteExclusions(const std::wstring& path, const std::vector<std::wstring>& keep) {
    if (path.empty()) return;
    std::wifstream f(path);
    if (!f.is_open()) return;
    std::vector<std::wstring> lines;
    std::wstring line;
    while (std::getline(f, line)) lines.push_back(line);
    f.close();

    std::vector<std::wstring> out;
    for (size_t i = 0; i < lines.size(); i++) {
        std::wstring trimmed = lines[i];
        size_t a = trimmed.find_first_not_of(L" \t\r\n");
        size_t b = trimmed.find_last_not_of(L" \t\r\n");
        trimmed = (a == std::wstring::npos) ? L"" : trimmed.substr(a, b - a + 1);
        if (trimmed == L"[[exclusion]]") {
            size_t j = i + 1;
            std::wstring exeName;
            while (j < lines.size()) {
                std::wstring t2 = lines[j];
                size_t a2 = t2.find_first_not_of(L" \t\r\n");
                size_t b2 = t2.find_last_not_of(L" \t\r\n");
                t2 = (a2 == std::wstring::npos) ? L"" : t2.substr(a2, b2 - a2 + 1);
                if (t2.rfind(L"[[", 0) == 0 || t2.rfind(L"[", 0) == 0) break;
                if (t2.rfind(L"exe", 0) == 0) {
                    size_t eq = t2.find(L'=');
                    if (eq != std::wstring::npos) {
                        std::wstring v = t2.substr(eq + 1);
                        size_t q1 = v.find(L'"');
                        size_t q2 = v.find(L'"', q1 + 1);
                        if (q1 != std::wstring::npos && q2 != std::wstring::npos)
                            exeName = WLower(v.substr(q1 + 1, q2 - q1 - 1));
                    }
                }
                j++;
            }
            bool keepIt = false;
            for (auto& e : keep) if (e == exeName) { keepIt = true; break; }
            if (!keepIt) { i = j - 1; continue; }
        }
        out.push_back(lines[i]);
    }

    std::wofstream fo(path, std::ios::trunc);
    for (auto& l : out) fo << l << L"\n";
}

KT_API int KT_AddExclusion(const wchar_t* exe) {
    if (!exe || !*exe) return 0;
    std::wstring name = WLower(exe);
    size_t bs = name.find_last_of(L"\\/");
    if (bs != std::wstring::npos) name = name.substr(bs + 1);
    int added = 0;
    std::wstring configPath;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        bool dup = false;
        for (auto& e : g_hostCfg.limiter_exclusions) if (e == name) { dup = true; break; }
        if (!dup) {
            g_hostCfg.limiter_exclusions.push_back(name);
            if (g_core) g_core->UpdateConfig(g_hostCfg);
            added = 1;
            configPath = g_hostCfg.file_path.empty() ? L"gamemode.toml" : g_hostCfg.file_path;
        }
    }
    if (added) {
        std::wofstream f(configPath, std::ios::app);
        if (f.is_open()) {
            f << L"\n[[exclusion]]\n";
            f << L"exe = \"" << name << L"\"\n";
        }
        Log(L"config", L"limiter exclusion added: %s", name.c_str());
    }
    return added;
}

KT_API int KT_RemoveExclusion(const wchar_t* exe) {
    if (!exe || !*exe) return 0;
    std::wstring name = WLower(exe);
    int removed = 0;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        for (auto it = g_hostCfg.limiter_exclusions.begin(); it != g_hostCfg.limiter_exclusions.end(); ++it) {
            if (*it == name) {
                g_hostCfg.limiter_exclusions.erase(it);
                if (g_core) g_core->UpdateConfig(g_hostCfg);
                removed = 1;
                break;
            }
        }
        if (removed)
            RewriteExclusions(g_hostCfg.file_path.empty() ? L"gamemode.toml" : g_hostCfg.file_path,
                              g_hostCfg.limiter_exclusions);
    }
    if (removed) Log(L"config", L"limiter exclusion removed: %s", name.c_str());
    return removed;
}

KT_API int KT_AddGame(const wchar_t* exe) {
    if (!exe || !*exe) return 0;
    std::wstring name = WLower(exe);
    size_t bs = name.find_last_of(L"\\/");
    if (bs != std::wstring::npos) name = name.substr(bs + 1);
    int added = 0;
    std::wstring configPath;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        bool dup = false;
        for (auto& g : g_hostCfg.custom_games) if (g == name) { dup = true; break; }
        if (!dup && !name.empty()) {
            g_hostCfg.custom_games.push_back(name);
            if (g_core) g_core->UpdateConfig(g_hostCfg);
            added = 1;
            configPath = g_hostCfg.file_path.empty() ? L"gamemode.toml" : g_hostCfg.file_path;
        }
    }
    if (added) {
        std::wofstream f(configPath, std::ios::app);
        if (f.is_open()) {
            f << L"\n[[game]]\n";
            f << L"exe = \"" << name << L"\"\n";
        }
    }
    return added;
}

KT_API void KT_LoadProfile(const wchar_t* name) {
    if (!name) return;
    std::wstring n = name;
    Config cfg;
    if (n.empty() || _wcsicmp(n.c_str(), L"normal") == 0 ||
        _wcsicmp(n.c_str(), L"default") == 0) {
        cfg = LoadCfgOrOverride();
    } else {
        wchar_t exeDir[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
        wchar_t* slash = wcsrchr(exeDir, L'\\');
        if (slash) *slash = 0;
        std::vector<std::wstring> candidates = {
            std::wstring(exeDir) + L"\\profiles\\" + n + L".toml",
            std::wstring(exeDir) + L"\\" + n + L".toml",
            L"profiles\\" + n + L".toml",
            n,
        };
        bool loaded = false;
        for (const auto& candidate : candidates) {
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                cfg = LoadConfigFrom(candidate);
                loaded = true;
                break;
            }
        }
        if (!loaded) cfg = LoadCfgOrOverride();
    }
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_hostCfg = cfg;
        if (g_core) g_core->UpdateConfig(g_hostCfg);
    }
}

KT_API int KT_ListProfiles(wchar_t* out, int maxCount, int nameLen) {
    if (!out || maxCount <= 0 || nameLen <= 1) return 0;
    wchar_t exeDir[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    wchar_t* slash = wcsrchr(exeDir, L'\\');
    if (slash) *slash = 0;
    std::wstring dir = std::wstring(exeDir) + L"\\profiles";
    auto names = ListProfiles(dir);
    int w = (int)((names.size() < (size_t)maxCount) ? names.size() : (size_t)maxCount);
    for (int i = 0; i < w; i++) {
        memset((char*)out + (size_t)i * nameLen * sizeof(wchar_t), 0, nameLen * sizeof(wchar_t));
        wcsncpy_s(out + (size_t)i * nameLen, nameLen, names[i].c_str(), _TRUNCATE);
    }
    return w;
}

KT_API ULONGLONG KT_CleanRam() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_core ? g_core->ForceCleanRam() : 0;
}

KT_API int KT_GetLogV2(int minLevel, int maxLines, KtLogLine* out) {
    if (!out || maxLines <= 0) return 0;
    std::vector<LogEntry> copy;
    {
        std::lock_guard<std::mutex> lk(g_logMtx);
        copy = g_logRing;
    }
    std::vector<LogEntry> filtered;
    for (auto& e : copy) {
        if (e.level >= minLevel) filtered.push_back(e);
    }
    if ((int)filtered.size() > maxLines) {
        filtered.erase(filtered.begin(), filtered.begin() + (filtered.size() - maxLines));
    }
    int n = (int)filtered.size();
    for (int i = 0; i < n; i++) {
        out[i].level = filtered[i].level;
        wcsncpy_s(out[i].text, 512, filtered[i].line.c_str(), _TRUNCATE);
    }
    return n;
}

KT_API int KT_GetMetricsHistory(KtMetricsSample* out, int maxCount) {
    if (!out || maxCount <= 0) return 0;
    std::vector<MetricsSample> snap;
    g_metrics.Snapshot(snap, (size_t)maxCount);
    int n = (int)snap.size();
    for (int i = 0; i < n; i++) {
        out[i].cpuPct     = snap[i].cpuPct;
        out[i].ramUsedMB  = snap[i].ramUsedMB;
        out[i].ramTotalMB = snap[i].ramTotalMB;
        out[i].fps        = snap[i].fps;
        out[i].pingMs     = snap[i].pingMs;
        out[i].tickMs     = snap[i].tickMs;
        out[i].gpuPct     = snap[i].gpuPct;
    }
    return n;
}

KT_API int KT_RunBench(unsigned int durationMs, KtBenchResult* out) {
    if (!out) return 0;
    BenchResult src;
    int fresh = 0;
    if (durationMs > 0) {
        src = RunBench((DWORD)durationMs);
        fresh = 1;
    } else {
        src = LatestBench();
    }
    out->latencyUsAvg = src.latencyUsAvg;
    out->latencyUsP50 = src.latencyUsP50;
    out->latencyUsP95 = src.latencyUsP95;
    out->latencyUsMax = src.latencyUsMax;
    out->samples      = src.samples;
    out->durationMs   = src.durationMs;
    out->tickMs       = src.tickMs;
    out->valid        = src.valid ? 1 : 0;
    return fresh;
}

KT_API void KT_SetConfigPath(const wchar_t* path) {
    std::lock_guard<std::mutex> lk(g_mtx);
    std::lock_guard<std::mutex> pathLock(g_cfgPathMtx);
    g_cfgOverridePath = path ? path : L"";
}

}
