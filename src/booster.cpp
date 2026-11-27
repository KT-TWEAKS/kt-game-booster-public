#define _CRT_SECURE_NO_WARNINGS
#include "booster.h"
#include "util.h"
#include "detect.h"
#include "procopt.h"
#include "threadopt.h"
#include "powersys.h"
#include "winsvcmon.h"
#include "platform.h"
#include <tlhelp32.h>
#include <dwmapi.h>
#include <algorithm>

using namespace kt;

namespace {
void DisableRemovedExperimentalTweaks(Config& cfg) {
    Features& f = cfg.features;
    f.memory_lock = false;
    f.pin_perf_cores = false;
    f.network_throttling_disable = false;
    f.timer_resolution_500us = false;
    f.cpu_sets_pin = false;
    f.kill_shell_processes = false;
    f.extreme_shell_mode = false;
    f.extreme_kill_dwm = false;
    f.auto_clean_ram = false;
    f.fso_disable = false;
}

void ApplyAntiCheatSafeMode(const std::wstring& gameName, Config& cfg, bool& outActive) {
    outActive = cfg.features.anticheat_safe_mode && IsAntiCheatProtectedGame(gameName);
    if (!outActive) return;
    Features& f = cfg.features;
    f.suspend_gameinput_threads = false;
    f.pause_services_gaming     = false;
    f.game_tree_boost           = false;
    f.background_limiter        = false;
    f.trim_background_ws        = false;
    f.thread_power_background_throttle = false;
}
}

static bool RuleMatchesGame(const ProcessRule& r, const std::wstring& exeLower,
                            const std::wstring& pathLower) {
    if (r.exe.empty() && r.path.empty()) return false;
    if (!r.exe.empty() && r.exe != exeLower) return false;
    if (!r.path.empty() && pathLower.find(WLower(r.path)) == std::wstring::npos) return false;
    return true;
}

static std::wstring ResolveProfileTomlPath(const std::wstring& profile) {
    std::vector<std::wstring> candidates;
    wchar_t exeDir[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(nullptr, exeDir, MAX_PATH)) {
        std::wstring dir(exeDir);
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos && slash > 0)
            candidates.push_back(dir.substr(0, slash) + L"\\profiles\\" + profile + L".toml");
    }
    candidates.push_back(L"profiles\\" + profile + L".toml");
    for (const auto& p : candidates)
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    return candidates.back();
}

static bool CaptureAppRunning() {
    static const wchar_t* kCaptureApps[] = {
        L"obs64.exe", L"obs32.exe", L"streamlabs obs.exe"
    };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring exe = WLower(pe.szExeFile);
            for (const wchar_t* app : kCaptureApps) {
                if (exe == app) { found = true; break; }
            }
        } while (!found && Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

BoosterCore::BoosterCore() {
    EnableDebugPrivilege();
    LogPlatformCapabilities(DetectPlatformCapabilities());
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_cpuCoreCount = si.dwNumberOfProcessors;
}

BoosterCore::~BoosterCore() {
    Stop();
    SetTimerResolution(false);
}

void BoosterCore::Start(const Config& cfg, const BoosterOptions& opt) {
    if (m_state.running.load()) return;
    if (m_safeMode.load()) {
        Log(L"safe", L"safe mode active: no tweaks were applied");
        return;
    }
    if (!m_snapshot.RecoverPendingJournal()) {
        Log(L"error", L"engine start blocked: pending session recovery failed");
        return;
    }
    Config effectiveCfg = cfg;
    DisableRemovedExperimentalTweaks(effectiveCfg);
    m_snapshot.Begin();
    m_opt = opt;
    if (m_opt.poll_interval_ms <= 0) m_opt.poll_interval_ms = (DWORD)effectiveCfg.poll_interval_ms;
    if (m_opt.poll_interval_ms < 50) m_opt.poll_interval_ms = 50;
    m_stop = false;
    {
        std::lock_guard<std::mutex> lk(m_state.cfgMutex);
        m_state.cfg = effectiveCfg;
    }
    m_state.running.store(true);
    m_state.paused.store(false);
    m_state.gamePid.store(0);
    m_state.gameActive.store(false);
    m_state.suspendedThreads.store(0);
    m_state.bgCount.store(0);
    m_state.etwEvents.store(0);
    m_state.totalRuns.store(0);
    m_state.memFreed.store(0);
    m_state.autoCleanCount.store(0);
    m_state.servicePauseCount.store(0);
    m_state.boostedChildCount.store(0);
    m_state.peakCpuPct.store(0);
    m_state.peakRamPct.store(0);
    m_state.fpsX100.store(0);
    m_state.frameP95Us.store(0);
    m_state.frameP99Us.store(0);
    m_state.frameSamples.store(0);
    m_state.startedTick.store(GetTickCount64());
    {
        std::lock_guard<std::mutex> lk(m_state.gameNameMutex);
        ZeroMemory(m_state.gameName, sizeof(m_state.gameName));
    }
    ClearGameProfile();
    m_restorePending = false;
    m_extremeMarkerCleanupPending = false;
    m_restoreShellRequested.store(false);
    m_restoreGameRequested.store(false);
    PublishProcessView();

    SetTimerResolution(true);

    m_driver.Init();
    m_lim.Configure(effectiveCfg.features.background_limiter, effectiveCfg.bgcpu.main_frame_cpu_pct,
                    effectiveCfg.bgcpu.worker_cpu_pct);

    m_thread = CreateThread(nullptr, 0, &BoosterCore::WorkerMain, this, 0, nullptr);
    if (!m_thread) {
        m_state.running.store(false);
        Log(L"error", L"engine worker thread could not be created (error %lu)", GetLastError());
    }
}

void BoosterCore::Stop() {
    m_stop = true;
    SetTimerResolution(false);
    m_fps.Stop();
    if (m_thread) {
        WaitForSingleObject(m_thread, INFINITE);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }
    m_etw.Stop();
    m_driver.Cleanup();
    m_state.anticheatSafe.store(false);
    m_state.anticheatKind.store(0);
    m_state.running.store(false);
}

void BoosterCore::SetPaused(bool p) {
    m_state.paused.store(p);
}

void BoosterCore::UpdateConfig(const Config& cfg) {
    Config effectiveCfg = cfg;
    DisableRemovedExperimentalTweaks(effectiveCfg);
    {
        std::lock_guard<std::mutex> lk(m_state.cfgMutex);
        m_state.cfg = effectiveCfg;
    }
    m_cfgDirty.store(true);
}

void BoosterCore::SetTestTarget(const std::wstring& exe) {
    std::lock_guard<std::mutex> lk(m_testMutex);
    m_testTarget = WLower(exe);
    Log(L"test", m_testTarget.empty() ? L"test target cleared" : L"test target: %s", m_testTarget.c_str());
}

void BoosterCore::StoreGameProfile(const std::wstring& name) {
    std::lock_guard<std::mutex> lk(m_state.gameProfileMutex);
    wcsncpy_s(m_state.gameProfile, 32, name.c_str(), _TRUNCATE);
}

void BoosterCore::SetTournament(bool on) {
    std::lock_guard<std::mutex> lk(m_state.cfgMutex);
    m_state.cfg.features.tournament_mode = on;
    m_cfgDirty.store(true);
    Log(L"mode", on ? L"TOURNAMENT MODE on" : L"tournament mode off");
}

void BoosterCore::RestoreShellNow() {
    if (m_state.running.load()) {
        m_restoreShellRequested.store(true);
        return;
    }
    if (m_snapshot.RecoverPendingJournal() && RecoverExtremeShell())
        m_state.extremeActive.store(false);
}

void BoosterCore::ReloadConfigIfChanged() {
    if (!m_cfgGet) return;
    Config fresh = m_cfgGet();
    if (!fresh.loaded_from_file || fresh.file_path.empty()) return;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(fresh.file_path.c_str(), GetFileExInfoStandard, &fad)) return;
    if (m_cfgMtime.dwLowDateTime == 0 && m_cfgMtime.dwHighDateTime == 0) {
        m_cfgMtime = fad.ftLastWriteTime;
        return;
    }
    if (fad.ftLastWriteTime.dwLowDateTime == m_cfgMtime.dwLowDateTime &&
        fad.ftLastWriteTime.dwHighDateTime == m_cfgMtime.dwHighDateTime)
        return;
    m_cfgMtime = fad.ftLastWriteTime;
    Log(L"config", L"%s changed on disk - hot reload", fresh.file_path.c_str());
    UpdateConfig(fresh);
}

ULONGLONG BoosterCore::ForceCleanRam() {
    ULONGLONG freed = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        DWORD gamePid = m_state.gamePid.load();
        do {
            if (pe.th32ProcessID == GetCurrentProcessId()) continue;
            if (gamePid && pe.th32ProcessID == gamePid) continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_QUOTA, FALSE,
                                   pe.th32ProcessID);
            if (!h) continue;
            PROCESS_MEMORY_COUNTERS_EX before{};
            before.cb = sizeof(before);
            SIZE_T beforeBytes = 0;
            if (GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS*)&before, sizeof(before)))
                beforeBytes = before.WorkingSetSize;
            if (TrimWorkingSet(h)) {
                PROCESS_MEMORY_COUNTERS_EX after{};
                after.cb = sizeof(after);
                if (GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS*)&after, sizeof(after)) &&
                    beforeBytes > after.WorkingSetSize)
                    freed += (ULONGLONG)(beforeBytes - after.WorkingSetSize);
            }
            CloseHandle(h);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    m_state.memFreed.store(m_state.memFreed.load() + freed);
    return freed;
}

void BoosterCore::ApplySystemToggles(const Config& cfg) {
    if (m_opt.dry_run) return;
    if (cfg.features.disable_c_states) {
        if (!m_cStatesApplied) { m_cStatesApplied = DisableCStates(m_snapshot); }
    } else if (m_cStatesApplied) {
        RestoreCStates();
        m_cStatesApplied = false;
    }
    if (cfg.features.high_performance_plan) {
        if (!m_powerPlanApplied) {
            if (!m_snapshot.CapturePowerPlan()) {
                Log(L"power", L"high-performance plan skipped: snapshot unavailable");
                return;
            }
            SavePowerState();
            if (SetHighPerformancePower()) m_powerPlanApplied = true;
            else Log(L"power", L"high-performance plan switch failed");
        }
    } else if (m_powerPlanApplied) {
        RestorePowerState();
        m_powerPlanApplied = false;
    }
    if (cfg.features.turbo_boost || cfg.features.core_parking_disable) {
            m_boostApplied = ApplyMaxBoost(cfg.features.turbo_boost,
                                           cfg.features.core_parking_disable, m_snapshot);
    } else if (m_boostApplied) {
        RestoreMaxBoost();
        m_boostApplied = false;
    }
    if (cfg.features.dwm_mmcss) {
        if (!m_dwmMmcssApplied) m_dwmMmcssApplied = EnableDwmMmcss();
    } else if (m_dwmMmcssApplied) {
        DisableDwmMmcss();
        m_dwmMmcssApplied = false;
    }
    if (cfg.features.dwm_thread_priority) {
        if (!m_dwmThreadsApplied) { BoostDwmThreads(m_snapshot); m_dwmThreadsApplied = true; }
    } else if (m_dwmThreadsApplied) {
        RestoreDwmThreads();
        m_dwmThreadsApplied = false;
    }
}

static void AccumulateSession(BoosterState& st) {
    ULONGLONG started = st.gameStartedTick.load();
    if (!started) return;
    ULONGLONG now = GetTickCount64();
    if (now > started)
        st.totalGameSeconds.store(st.totalGameSeconds.load() + (now - started) / 1000);
    st.gameStartedTick.store(0);
}

static void OptimizeGamingServices(BoosterState& st, GameCtx& game, SessionSnapshot& snapshot);
static void RestoreGamingServices(BoosterState& st, GameCtx& game);

DWORD WINAPI BoosterCore::WorkerMain(LPVOID p) {
    BoosterCore* self = (BoosterCore*)p;
    self->Run();
    return 0;
}

void BoosterCore::Run() {
    Config cfg = [this] {
        std::lock_guard<std::mutex> lk(m_state.cfgMutex);
        return m_state.cfg;
    }();

    ApplySystemToggles(cfg);

    if (cfg.features.thread_power_game_unthrottle) {
        if (!m_etw.Start())
            Log(L"warning", L"ETW unavailable, falling back to polling (%u ms)", m_opt.poll_interval_ms);
    }

    int cycle = 0;
    ULONGLONG lastBackgroundRefresh = 0;
    bool preserveReconfiguredSession = false;
    ULONGLONG preservedGameStartedTick = 0;
    while (!m_stop) {
        if (m_restoreShellRequested.exchange(false)) {
            if (m_game.extremeActive || !m_game.extremeWinlogonPids.empty()) {
                if (RevertExtremeShell(m_game.extremeWinlogonPids, m_snapshot)) {
                    m_game.extremeActive = false;
                    m_game.extremeWinlogonPids.clear();
                    m_state.extremeActive.store(false);
                    m_extremeMarkerCleanupPending = !m_snapshot.ClearExtremeActive();
                    Log(L"shell", L"legacy shell state restored (panic recovery)");
                } else {
                    Log(L"error", L"panic restore incomplete; retrying shell recovery");
                    m_restoreShellRequested.store(true);
                }
            } else if (!m_game.pid && !m_state.gameActive.load()) {
                RecoverExtremeShell();
            }
        }
        if (m_extremeMarkerCleanupPending)
            m_extremeMarkerCleanupPending = !m_snapshot.ClearExtremeActive();

        if (m_restorePending) {
            if (!m_snapshot.RestoreAll()) {
                PublishProcessView();
                Sleep(m_opt.poll_interval_ms);
                continue;
            }
            m_restorePending = false;
            m_game.extremeActive = false;
            m_game.extremeWinlogonPids.clear();
            m_state.extremeActive.store(false);
            m_cStatesApplied = false;
            m_powerPlanApplied = false;
            m_boostApplied = false;
            m_dwmThreadsApplied = false;
            {
                std::lock_guard<std::mutex> lk(m_state.cfgMutex);
                cfg = m_state.cfg;
            }
            ApplySystemToggles(cfg);
        }

        if (m_restoreGameRequested.exchange(false) && m_game.pid) {
            AccumulateSession(m_state);
            RestoreGame(m_game, m_throttled);
            m_state.gameActive.store(false);
            m_state.gamePid.store(0);
            m_state.gameStartedTick.store(0);
            ClearGameProfile();
        }

        if (m_cfgDirty.exchange(false)) {
            Config next;
            {
                std::lock_guard<std::mutex> lk(m_state.cfgMutex);
                next = m_state.cfg;
            }
            if (m_game.pid) {
                std::wstring currentGame = m_game.name;
                preservedGameStartedTick = m_state.gameStartedTick.load();
                RestoreGame(m_game, m_throttled);
                {
                    std::lock_guard<std::mutex> lk(m_testMutex);
                    m_testTarget = currentGame;
                }
                preserveReconfiguredSession = true;
            }
            cfg = next;
            ApplySystemToggles(cfg);
            m_lim.Configure(cfg.features.background_limiter, cfg.bgcpu.main_frame_cpu_pct,
                            cfg.bgcpu.worker_cpu_pct);
        }

        if (cycle % 10 == 0) ReloadConfigIfChanged();

        bool paused = m_state.paused.load();
        if (paused) {
            if (m_game.pid) {
                AccumulateSession(m_state);
                RestoreGame(m_game, m_throttled);
                m_state.gameActive.store(false);
                m_state.gamePid.store(0);
                m_state.gameStartedTick.store(0);
                ClearGameProfile();
            }
            Sleep(100);
            continue;
        }

        if (m_restorePending) {
            PublishProcessView();
            Sleep(m_opt.poll_interval_ms);
            continue;
        }

        if (m_game.pid == 0) {
            std::wstring forcedExe;
            {
                std::lock_guard<std::mutex> lk(m_testMutex);
                forcedExe = m_testTarget;
            }
            auto results = DetectGamePidsEx(cfg, forcedExe);
            if (!results.empty()) {
                const auto& best = results[0];
                m_game.pid = best.pid;
                m_game.name = best.name;
                m_game.h = OpenProcessForOpt(m_game.pid);
                if (m_game.h) {
                    GetProcPriorityClass(m_game.h, &m_game.origPriority);
                    GetProcAffinity(m_game.h, &m_game.origSys, &m_game.origProc);
                    m_game.savedThreads = EnumerateThreads(m_game.pid);
                    if (!m_snapshot.CaptureProcess(m_game.h, m_game.pid)) {
                        for (auto& t : m_game.savedThreads) if (t.handle) CloseHandle(t.handle);
                        m_game.savedThreads.clear();
                        CloseHandle(m_game.h);
                        m_game.h = nullptr;
                        m_game.pid = 0;
                        Log(L"error", L"game optimization skipped: unable to persist snapshot");
                        continue;
                    }
                    if (!m_snapshot.CaptureThreads(m_game.savedThreads)) {
                        for (auto& t : m_game.savedThreads) if (t.handle) CloseHandle(t.handle);
                        m_game.savedThreads.clear();
                        CloseHandle(m_game.h);
                        m_game.h = nullptr;
                        m_game.pid = 0;
                        Log(L"error", L"game optimization skipped: unable to persist thread snapshot");
                        continue;
                    }
                    m_state.detectionConfidence.store(best.confidence);
                    Log(L"game", L"Optimizing %s (PID %u) [confidence: %d%%, method: %hs]",
                        m_game.name.c_str(), m_game.pid, best.confidence, best.method.c_str());

                    std::wstring autoPreset;
                    if (cfg.features.auto_profile_switching && !cfg.features.tournament_mode) {
                        Config fresh = cfg;
                        fresh.features.tournament_mode = true;
                        fresh.features.dwm_mmcss                   = true;
                        fresh.features.dwm_thread_priority         = true;
                        fresh.features.gpu_priority                = true;
                        fresh.features.turbo_boost                 = true;
                        fresh.features.core_parking_disable        = true;
                        fresh.features.disable_c_states            = false;
                        fresh.features.io_priority_game_high       = true;
                        fresh.features.mmcss_game                  = true;
                        fresh.features.page_priority_game          = true;
                        fresh.features.memory_priority_game_high   = true;
                        fresh.features.boost_input_threads         = true;
                        fresh.features.game_tree_boost             = true;
                        fresh.features.suspend_gameinput_threads   = false;
                        DisableRemovedExperimentalTweaks(fresh);
                        cfg = fresh;
                        {
                            std::lock_guard<std::mutex> lk(m_state.cfgMutex);
                            m_state.cfg = fresh;
                        }
                        ApplySystemToggles(fresh);
                        m_lim.Configure(fresh.features.background_limiter, fresh.bgcpu.main_frame_cpu_pct,
                                        fresh.bgcpu.worker_cpu_pct);
                        Log(L"profile", L"auto-profile: switched to tournament (game detected)");
                        autoPreset = L"tournament";
                    }

                    bool ruleApplied = false;
                    bool sessionProfileSwapped = false;
                    for (const auto& rule : cfg.processes) {
                        if (!RuleMatchesGame(rule, m_game.name, GetProcPathByHandle(m_game.h)) || rule.profile.empty()) continue;
                        const std::wstring profileName = rule.profile;
                        Config perGame = LoadConfigFrom(ResolveProfileTomlPath(profileName));
                        if (perGame.loaded_from_file) {
                            DisableRemovedExperimentalTweaks(perGame);
                            autoPreset = profileName;
                            ruleApplied = true;
                            sessionProfileSwapped = true;
                            cfg = perGame;
                            Log(L"profile", L"per-game profile '%s' applied to %s", profileName.c_str(), m_game.name.c_str());
                        } else Log(L"warning", L"per-game profile '%s' unavailable; global profile retained", profileName.c_str());
                        break;
                    }

                    if (!ruleApplied && CaptureAppRunning()) {
                        Config streaming = LoadConfigFrom(ResolveProfileTomlPath(L"streaming"));
                        if (streaming.loaded_from_file) {
                            DisableRemovedExperimentalTweaks(streaming);
                            cfg = streaming;
                            autoPreset = L"streaming";
                            sessionProfileSwapped = true;
                            Log(L"profile", L"streaming preset: capture app running in background");
                        }
                    }
                    StoreGameProfile(autoPreset);

                    if (sessionProfileSwapped) {
                        ApplySystemToggles(cfg);
                        m_lim.Configure(cfg.features.background_limiter, cfg.bgcpu.main_frame_cpu_pct,
                                        cfg.bgcpu.worker_cpu_pct);
                    }

                    bool acSafe = false;
                    ApplyAntiCheatSafeMode(m_game.name, cfg, acSafe);
                    {
                        std::lock_guard<std::mutex> lk(m_state.cfgMutex);
                        m_state.cfg = cfg;
                    }
                    if (acSafe) {
                        m_state.anticheatSafe.store(true);
                        m_state.anticheatKind.store(AntiCheatKind());
                        Log(L"profile", L"anticheat safe mode: %s -> suspension/services/bg-limiter OFF this session",
                            m_game.name.c_str());
                    } else {
                        m_state.anticheatSafe.store(false);
                        m_state.anticheatKind.store(0);
                    }

                    ApplyGameOptimizations(m_game, cfg, m_opt);

                    if (cfg.features.fps_monitor)
                        m_fps.Start(m_game.pid);

                    std::vector<DWORD> bg;
                    CollectBackgroundPids(m_game, cfg, bg);
                    Log(L"bg", L"%d background processes to limit", (int)bg.size());
                    ApplyBackgroundLimits(bg, cfg, m_opt, &m_throttled);
                    lastBackgroundRefresh = GetTickCount64();

                    m_state.gamePid.store(m_game.pid);
                    m_state.gameActive.store(true);
                    m_state.gameStartedTick.store(preserveReconfiguredSession && preservedGameStartedTick
                        ? preservedGameStartedTick : GetTickCount64());
                    if (!preserveReconfiguredSession) {
                        m_state.totalRuns.store(m_state.totalRuns.load() + 1);
                        m_state.totalGameSessions.store(m_state.totalGameSessions.load() + 1);
                    }
                    preserveReconfiguredSession = false;
                    preservedGameStartedTick = 0;
                    {
                        std::lock_guard<std::mutex> lk(m_state.gameNameMutex);
                        wcsncpy_s(m_state.gameName, m_game.name.c_str(), _TRUNCATE);
                    }

                    ApplyGameTreeBoost(m_game, cfg);
                    if (cfg.features.pause_services_gaming && !m_opt.dry_run)
                        OptimizeGamingServices(m_state, m_game, m_snapshot);

                    { std::lock_guard<std::mutex> lk(m_testMutex); m_testTarget.clear(); }
                } else {
                    m_game.pid = 0;
                }
            }
        } else {
            if (!IsProcessRunning(m_game.pid)) {
                AccumulateSession(m_state);
                RestoreGame(m_game, m_throttled);
                m_state.gameActive.store(false);
                m_state.gamePid.store(0);
                m_state.gameStartedTick.store(0);
                ClearGameProfile();
                continue;
            }

            FpsStats fs;
            if (m_fps.GetStats(&fs)) {
                m_state.fpsX100.store((DWORD)(fs.fps * 100.0 + 0.5));
                m_state.frameP95Us.store((DWORD)(fs.p95FrameMs * 1000.0 + 0.5));
                m_state.frameP99Us.store((DWORD)(fs.p99FrameMs * 1000.0 + 0.5));
                m_state.frameSamples.store(fs.samples);
            } else {
                m_state.fpsX100.store(0);
                m_state.frameSamples.store(0);
            }

            const bool servicesChanged = !m_game.pausedGamingServices.empty() ||
                                         !m_game.stoppedGamingServices.empty();
            if (cfg.features.pause_services_gaming && !servicesChanged && !m_opt.dry_run)
                OptimizeGamingServices(m_state, m_game, m_snapshot);
            else if (!cfg.features.pause_services_gaming && servicesChanged)
                RestoreGamingServices(m_state, m_game);
            if (!m_opt.dry_run) {
                std::wstring gamePath = m_game.h ? GetProcPathByHandle(m_game.h) : L"";
                PriorityClass target = PriorityClass::High;
                if (cfg.features.tournament_mode) target = PriorityClass::High;
                for (const auto& r : cfg.processes)
                    if (RuleMatchesGame(r, m_game.name, gamePath) && r.has_priority) { target = r.priority; break; }
                SetProcPriorityClass(m_game.h, target);
                int originalGpuPriority = 0;
                const bool originalGpuKnown =
                    m_snapshot.GetGpuPrioritySnapshot(m_game.pid, &originalGpuPriority);
                if (cfg.features.gpu_priority) {
                    if (originalGpuKnown && !m_game.gpuPriorityApplied) {
                        if (SetGpuPriority(m_game.h, true)) {
                            m_game.gpuPriorityApplied = true;
                            m_game.gpuPriorityRestoreWarned = false;
                            Log(L"gpu", L"GPU priority set to HIGH (enabled during session)");
                        }
                    } else if (m_game.gpuPriorityApplied) {
                        SetGpuPriority(m_game.h, true);
                        m_game.gpuPriorityRestoreWarned = false;
                    }
                } else if (m_game.gpuPriorityApplied) {
                    if (originalGpuKnown &&
                        SetGpuSchedulingPriority(m_game.h, originalGpuPriority)) {
                        m_game.gpuPriorityApplied = false;
                        m_game.gpuPriorityRestoreWarned = false;
                        Log(L"gpu", L"original GPU priority restored (disabled during session)");
                    } else if (!m_game.gpuPriorityRestoreWarned) {
                        m_game.gpuPriorityRestoreWarned = true;
                        Log(L"warning", L"GPU priority restore deferred; retrying");
                    }
                }

                auto fresh = m_etw.TakeNewThreads();
                int gameThreadEvents = 0;
                for (const auto& event : fresh)
                    if (event.first == m_game.pid) gameThreadEvents++;
                if (gameThreadEvents > 0) {
                    Log(L"etw", L"%d new game thread event(s), re-optimizing", gameThreadEvents);
                    OptimizeGameThreads(m_game.pid, cfg, &m_snapshot);
                } else if (cycle % 20 == 0) {
                    OptimizeGameThreads(m_game.pid, cfg, &m_snapshot);
                }
                if (cycle % 10 == 5) ApplyGameTreeBoost(m_game, cfg);
                auto newProcs = m_etw.TakeNewProcesses();
                if (!newProcs.empty()) {
                    for (auto& np : newProcs) {
                        std::wstring exeLower = WLower(np.second);
                        for (auto& g : cfg.custom_games) {
                            if (g == exeLower) {
                                Log(L"etw", L"new game process detected: %s (PID %u)", np.second.c_str(), np.first);
                                break;
                            }
                        }
                    }
                }
                if (cfg.features.dwm_mmcss) DwmEnableMMCSS(TRUE);

                if (m_game.extremeActive || !m_game.extremeWinlogonPids.empty()) {
                    if (RevertExtremeShell(m_game.extremeWinlogonPids, m_snapshot)) {
                        m_game.extremeWinlogonPids.clear();
                        m_game.extremeActive = false;
                        m_state.extremeActive.store(false);
                        m_extremeMarkerCleanupPending = !m_snapshot.ClearExtremeActive();
                        Log(L"shell", L"legacy shell takeover restored");
                    } else {
                        Log(L"error", L"legacy shell restore incomplete; retrying");
                    }
                }
            }
            ULONGLONG bgNow = GetTickCount64();
            bool limiterTurnedOff = !cfg.features.background_limiter && !m_throttled.empty();
            bool refreshDue = cfg.features.background_limiter &&
                              (!lastBackgroundRefresh || bgNow - lastBackgroundRefresh >= 2000);
            if (limiterTurnedOff || refreshDue) {
                RefreshBackground(m_game, cfg, m_opt, &m_throttled);
                lastBackgroundRefresh = bgNow;
            }
            m_lim.Tick(m_opt.poll_interval_ms, &m_snapshot);
        }

        cycle++;
        m_state.suspendedThreads.store((DWORD)m_lim.CurrentSuspended());
        m_state.etwEvents.store(m_etw.EventCount());
        m_state.bgCount.store((DWORD)m_throttled.size());
        PublishProcessView();

        Sleep(m_opt.poll_interval_ms);
    }

    AccumulateSession(m_state);
    RestoreGame(m_game, m_throttled);
    m_fps.Stop();
    if (m_cStatesApplied) { RestoreCStates(); m_cStatesApplied = false; }
    if (m_boostApplied) { RestoreMaxBoost(); m_boostApplied = false; }
    if (m_powerPlanApplied) { RestorePowerState(); m_powerPlanApplied = false; }
    if (m_dwmMmcssApplied) { DisableDwmMmcss(); m_dwmMmcssApplied = false; }
    if (m_dwmThreadsApplied) { RestoreDwmThreads(); m_dwmThreadsApplied = false; }
    m_snapshot.RestoreAll();
    m_etw.Stop();
    m_state.gameActive.store(false);
    m_state.gamePid.store(0);
    m_state.gameStartedTick.store(0);
    ClearGameProfile();
}

void BoosterCore::ApplyGameOptimizations(GameCtx& g, const Config& cfg, const BoosterOptions& opt) {
    std::wstring gamePath = g.h ? GetProcPathByHandle(g.h) : L"";

    Config eff = cfg;
    if (cfg.features.tournament_mode) {
        eff.features.mmcss_game = true;
        eff.features.page_priority_game = true;
        eff.features.memory_priority_game_high = true;
        Log(L"mode", L"tournament profile active");
    }

    PriorityClass target = PriorityClass::High;
    if (cfg.features.tournament_mode) target = PriorityClass::High;
    for (const auto& r : cfg.processes) {
        if (RuleMatchesGame(r, g.name, gamePath) && r.has_priority) { target = r.priority; break; }
    }
    if (!opt.dry_run) {
        if (SetProcPriorityClass(g.h, target)) {
            g.appliedPriority = target;
            Log(L"class", L"game priority class: %s", PriorityClassName(target));
        } else {
            Log(L"error", L"game priority class change failed");
        }
    }

    bool affApplied = false;
    for (const auto& r : cfg.processes) {
        if (!RuleMatchesGame(r, g.name, gamePath)) continue;
        DWORD_PTR sys = 0, proc = 0;
        GetProcAffinity(g.h, &sys, &proc);
        if (r.has_affinity_cores && !r.affinity_cores.empty()) {
            DWORD_PTR mask = 0;
            for (int c : r.affinity_cores) {
                if (c >= 0 && c < (int)(sizeof(DWORD_PTR) * 8)) mask |= ((DWORD_PTR)1 << c);
            }
            if (mask && sys && SetProcAffinity(g.h, mask & sys)) {
                Log(L"topo", L"affinity mask applied: 0x%llX", (unsigned long long)(mask & sys));
                affApplied = true;
            } else Log(L"warning", L"affinity mask could not be applied");
        } else if (r.affinity_ccd >= -1 && sys) {
            DWORD_PTR mask = 0;
            int ideal = -1;
            if (BuildTopologyMask(r.affinity_ccd, r.affinity_smt, sys, &mask, &ideal)) {
                if (!SetProcAffinity(g.h, mask)) {
                    Log(L"warning", L"affinity CCD=%d could not be applied", r.affinity_ccd);
                    break;
                }
                Log(L"topo", L"affinity CCD=%d SMT=%s mask=0x%llX",
                    r.affinity_ccd, r.affinity_smt ? L"on" : L"off",
                    (unsigned long long)mask);
            if (r.has_ideal && r.ideal_processor < 0 && ideal >= 0) {
                    auto th = EnumerateThreads(g.pid);
                    const bool capturedThreads = m_snapshot.CaptureThreads(th);
                    for (auto& t : th)
                        if (capturedThreads && t.handle && t.idealProcessorKnown)
                            SetThreadIdealProcessor(t.handle, (DWORD)ideal);
                    for (auto& t : th) if (t.handle) CloseHandle(t.handle);
                }
                affApplied = true;
            } else {
                Log(L"topo", L"topology unavailable for CCD=%d (ignored)", r.affinity_ccd);
            }
        }
        if (r.has_ideal && r.ideal_processor >= 0) {
            auto th = EnumerateThreads(g.pid);
            const bool capturedThreads = m_snapshot.CaptureThreads(th);
            for (auto& t : th)
                if (capturedThreads && t.handle && t.idealProcessorKnown)
                    SetThreadIdealProcessor(t.handle, (DWORD)r.ideal_processor);
            for (auto& t : th) if (t.handle) CloseHandle(t.handle);
        }
        break;
    }

    if (cfg.features.gpu_priority && !opt.dry_run) {
        if (!m_snapshot.HasGpuPrioritySnapshot(g.pid)) {
            Log(L"gpu", L"GPU priority skipped: original scheduling class could not be captured");
        } else if (SetGpuPriority(g.h, true)) {
            Log(L"gpu", L"GPU priority set to HIGH");
            g.gpuPriorityApplied = true;
        } else
            Log(L"gpu", L"D3DKMTSetProcessSchedulingPriorityClass failed (ignored)");
    }

    if (eff.features.io_priority_game_high && !opt.dry_run) {
        g.origIoKnown = GetProcIoPriority(g.h, &g.origIoPriority);
        if (SetProcIoPriority(g.h, 3)) {
            g.ioHighApplied = true;
            Log(L"io", L"I/O: High");
        } else Log(L"error", L"I/O priority change failed");
    }

    if (eff.features.memory_priority_game_high && !opt.dry_run) {
        g.origMemKnown = GetProcMemoryPriority(g.h, &g.origMemPriority);
        if (SetProcMemoryPriority(g.h, 5)) {
            g.memPrioHighApplied = true;
            Log(L"mem", L"memory priority: HIGH");
        }
    }

    if (!opt.dry_run) {
        g.origEcoKnown = GetProcEcoQoS(g.h, &g.origEcoQoS);
        if (!SetProcEcoQoS(g.h, false)) Log(L"warning", L"EcoQoS removal unavailable");
        OptimizeGameThreads(g.pid, eff, &m_snapshot);
    }

    if (eff.features.suspend_gameinput_threads && !opt.dry_run) {
        static const wchar_t* kGameInput[] = {
            L"gameinputsvc.exe", L"gameinputredistservice.exe", nullptr
        };
        for (int i = 0; kGameInput[i]; i++) {
            for (DWORD pid : FindPidsByName(kGameInput[i])) {
                if (pid == GetCurrentProcessId() || pid == g.pid) continue;
                if (std::find(g.gameInputPids.begin(), g.gameInputPids.end(), pid) != g.gameInputPids.end())
                    continue;
                HANDLE h = OpenProcessForOpt(pid);
                if (!h) continue;
                if (m_snapshot.CaptureProcess(h, pid) && SuspendProcessThreads(pid, m_snapshot)) {
                    g.gameInputPids.push_back(pid);
                    Log(L"gameinput", L"suspended %s (PID %u)", kGameInput[i], pid);
                }
                CloseHandle(h);
            }
        }
    }

    if (eff.features.memory_compression_disable && !opt.dry_run) {
        if (DisableMemoryCompression(m_snapshot)) {
            g.memoryCompressionDisabled = true;
            Log(L"memory", L"memory compression reduced");
        }
    }

    if (eff.features.gpu_hardware_scheduling && !opt.dry_run) {
        if (!IsHwGpuSchedulingEnabled()) {
            if (SetHwGpuScheduling(true, m_snapshot))
                Log(L"gpu", L"hardware GPU scheduling enabled (requires restart)");
            else
                Log(L"gpu", L"hardware GPU scheduling: access denied (requires admin)");
        }
    }

    if (eff.features.gaming_power_plans && !opt.dry_run) {
        GUID gamePlan;
        if (GuidFromString(cfg.power_plan_game_guid, &gamePlan)) {
            if (!m_snapshot.CapturePowerPlan()) {
                Log(L"power", L"gaming power plan skipped: snapshot unavailable");
            } else if (ApplyGamingPowerPlan(gamePlan))
                g.gamingPlanApplied = true;
        }
    }

    if (eff.features.game_dvr_disable && !opt.dry_run) {
        if (DisableGameDvr(m_snapshot))
            g.gameDvrApplied = true;
    }
}

void BoosterCore::CollectBackgroundPids(const GameCtx& g, const Config& cfg, std::vector<DWORD>& bg) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            DWORD pid = pe.th32ProcessID;
            if (pid == g.pid || pid == GetCurrentProcessId()) continue;
            std::wstring n = WLower(pe.szExeFile);
            if (IsSystemWhitelisted(n) || IsAntiCheatWhitelisted(n) || IsSecurityWhitelisted(n)) continue;
            if (IsProtectedByConfig(cfg, n)) continue;
            for (auto& e : cfg.limiter_exclusions) if (e == n) { n.clear(); break; }
            if (n.empty()) continue;
            bool child = false;
            for (DWORD c : g.boostedChildren) if (c == pid) { child = true; break; }
            if (child) continue;
            if (!MatchGameName(n, cfg).empty()) continue;
            bg.push_back(pid);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

void BoosterCore::ApplyBackgroundLimits(const std::vector<DWORD>& bg, const Config& cfg,
                                        const BoosterOptions& opt, std::set<DWORD>* throttled) {
    if (opt.dry_run) return;
    if (!cfg.features.background_limiter) return;

    DWORD_PTR pinMask = 0, pinSys = 0;
    if (cfg.features.background_cpu_pin) {
        DWORD_PTR sys = 0, proc = 0;
        if (GetProcAffinity(GetCurrentProcess(), &sys, &proc)) pinSys = sys;
        if (!cfg.bgcpu.cpus.empty()) {
            for (int c : cfg.bgcpu.cpus)
                if (c >= 0 && c < (int)(sizeof(DWORD_PTR) * 8)) pinMask |= ((DWORD_PTR)1 << c);
            pinMask &= pinSys;
        }
        if (!pinMask) Log(L"bg", L"background_cpu_pin: no cpus configured, skipping");
    }

    for (DWORD pid : bg) {
        if (throttled->count(pid)) continue;
        HANDLE h = OpenProcessForOpt(pid);
        if (!h) continue;
        if (!m_snapshot.CaptureProcess(h, pid)) {
            CloseHandle(h);
            Log(L"warning", L"background PID %lu skipped: snapshot unavailable", pid);
            continue;
        }
        PriorityClass origPriority = PriorityClass::None;
        if (GetProcPriorityClass(h, &origPriority)) m_bgOrigPriority[pid] = origPriority;
        int origValue = 0;
        if (GetProcIoPriority(h, &origValue)) m_bgOrigIoPriority[pid] = origValue;
        if (GetProcMemoryPriority(h, &origValue)) m_bgOrigMemoryPriority[pid] = origValue;
        bool origEco = false;
        if (GetProcEcoQoS(h, &origEco)) m_bgOrigEcoQoS[pid] = origEco;
        if (!SetProcPriorityClass(h, PriorityClass::Idle)) Log(L"warning", L"background priority failed for PID %lu", pid);
        if (cfg.features.io_priority_background && !SetProcIoPriority(h, 1)) Log(L"warning", L"background I/O change failed for PID %lu", pid);
        if (cfg.features.eco_qos_background && !SetProcEcoQoS(h, true)) Log(L"warning", L"background EcoQoS change failed for PID %lu", pid);
        if (cfg.features.memory_priority_background && !SetProcMemoryPriority(h, 1)) Log(L"warning", L"background memory priority failed for PID %lu", pid);
        if (cfg.features.trim_background_ws) TrimWorkingSet(h);
        if (pinMask) {
            DWORD_PTR sys = 0, proc = 0;
            if (GetProcAffinity(h, &sys, &proc) && proc) {
                m_bgOrigAffinity[pid] = proc;
                SetProcessAffinityMask(h, pinMask);
            }
        }
        CloseHandle(h);
        if (cfg.features.background_limiter)
            m_lim.AddProcess(pid);
        throttled->insert(pid);
    }
    if (cfg.features.background_limiter) {
        for (DWORD pid : bg) {
            HANDLE h = OpenProcessForOpt(pid);
            if (!h) continue;
        ThrottleBackgroundThreads(pid, cfg, &m_snapshot);
            CloseHandle(h);
        }
    }
}

void BoosterCore::UnlimitBackground(DWORD pid, const Config& cfg, std::set<DWORD>* throttled) {
    HANDLE h = OpenProcessForOpt(pid);
    if (h) {
        auto pri = m_bgOrigPriority.find(pid);
        if (pri != m_bgOrigPriority.end()) SetProcPriorityClass(h, pri->second);
        auto eco = m_bgOrigEcoQoS.find(pid);
        if (eco != m_bgOrigEcoQoS.end()) SetProcEcoQoS(h, eco->second);
        auto io = m_bgOrigIoPriority.find(pid);
        if (io != m_bgOrigIoPriority.end()) SetProcIoPriority(h, io->second);
        auto mem = m_bgOrigMemoryPriority.find(pid);
        if (mem != m_bgOrigMemoryPriority.end()) SetProcMemoryPriority(h, mem->second);
        auto it = m_bgOrigAffinity.find(pid);
        if (it != m_bgOrigAffinity.end()) {
            SetProcessAffinityMask(h, it->second);
            m_bgOrigAffinity.erase(it);
        }
        CloseHandle(h);
    }
    m_bgOrigPriority.erase(pid);
    m_bgOrigEcoQoS.erase(pid);
    m_bgOrigIoPriority.erase(pid);
    m_bgOrigMemoryPriority.erase(pid);
    m_bgOrigAffinity.erase(pid);
    RestoreBackgroundThreads(pid, m_snapshot);
    m_lim.RemoveProcess(pid, &m_snapshot);
    if (throttled) throttled->erase(pid);
}

void BoosterCore::RefreshBackground(const GameCtx& g, const Config& cfg,
                                    const BoosterOptions& opt, std::set<DWORD>* throttled) {
    if (opt.dry_run) return;
    if (!cfg.features.background_limiter) {
        for (DWORD pid : *throttled) UnlimitBackground(pid, cfg, nullptr);
        m_lim.Clear(&m_snapshot);
        throttled->clear();
        return;
    }

    std::vector<DWORD> bg;
    CollectBackgroundPids(g, cfg, bg);
    std::set<DWORD> bgSet(bg.begin(), bg.end());

    for (auto it = throttled->begin(); it != throttled->end();) {
        DWORD pid = *it;
        if (bgSet.count(pid) == 0 || !IsProcessRunning(pid)) {
            UnlimitBackground(pid, cfg, nullptr);
            it = throttled->erase(it);
        } else {
            ++it;
        }
    }

    std::vector<DWORD> fresh;
    for (DWORD pid : bg)
        if (!throttled->count(pid)) fresh.push_back(pid);
    if (!fresh.empty())
        ApplyBackgroundLimits(fresh, cfg, opt, throttled);
}

void BoosterCore::RestoreGame(GameCtx& g, std::set<DWORD>& throttled) {
    if (!g.pid) return;
    m_state.anticheatSafe.store(false);
    m_state.anticheatKind.store(0);
    RevertGameTreeBoost(g);
    m_fps.Stop();
    m_state.fpsX100.store(0);
    m_state.frameP95Us.store(0);
    m_state.frameP99Us.store(0);
    m_state.frameSamples.store(0);
    if (g.gameDvrApplied) {
        RestoreGameDvr();
        g.gameDvrApplied = false;
    }
    if (!g.pausedGamingServices.empty() || !g.stoppedGamingServices.empty())
        RestoreGamingServices(m_state, g);
    if (g.h) {
        if (g.origPriority != PriorityClass::None)
            SetProcPriorityClass(g.h, g.origPriority);
        if (g.origProc)
            SetProcessAffinityMask(g.h, g.origProc);
        if (g.ioHighApplied && g.origIoKnown)
            SetProcIoPriority(g.h, g.origIoPriority);
        if (g.memPrioHighApplied && g.origMemKnown)
            SetProcMemoryPriority(g.h, g.origMemPriority);
        if (g.origEcoKnown)
            SetProcEcoQoS(g.h, g.origEcoQoS);
        CloseHandle(g.h);
        g.h = nullptr;
    }
    g.ioHighApplied = false;
    g.memPrioHighApplied = false;
    g.origIoKnown = false;
    g.origMemKnown = false;
    g.origEcoKnown = false;
    g.gpuPriorityApplied = false;
    g.gpuPriorityRestoreWarned = false;

    if (g.memoryCompressionDisabled) {
        RestoreMemoryCompression(m_snapshot);
        g.memoryCompressionDisabled = false;
        Log(L"memory", L"memory compression restored");
    }

    if (g.gamingPlanApplied) {
        RestoreGamingPowerPlan();
        g.gamingPlanApplied = false;
    }
    if (g.extremeActive) {
        if (RevertExtremeShell(g.extremeWinlogonPids, m_snapshot)) {
            g.extremeWinlogonPids.clear();
            g.extremeActive = false;
            m_state.extremeActive.store(false);
            m_extremeMarkerCleanupPending = !m_snapshot.ClearExtremeActive();
            Log(L"shell", L"legacy shell takeover recovered at game end");
        } else {
            Log(L"error", L"extreme restore incomplete at game end; journal kept for retry");
        }
    }
    m_snapshot.RestoreProcessThreads(g.pid);
    for (auto& t : g.savedThreads) if (t.handle) CloseHandle(t.handle);
    g.savedThreads.clear();
    Log(L"game", L"process state restored");
    g.pid = 0;
    g.name.clear();
    g.appliedPriority = PriorityClass::None;
    m_state.detectionConfidence.store(0);

    for (DWORD pid : g.gameInputPids) {
            if (m_snapshot.RestoreProcessThreads(pid))
                Log(L"gameinput", L"restored threads for PID %u", pid);
    }
    g.gameInputPids.clear();

    m_lim.Clear(&m_snapshot);
    Config cfg;
    {
        std::lock_guard<std::mutex> lk(m_state.cfgMutex);
        cfg = m_state.cfg;
    }
    for (DWORD pid : throttled) UnlimitBackground(pid, cfg, nullptr);
    throttled.clear();
    Log(L"bg", L"background processes restored");
    if (m_cStatesApplied) { RestoreCStates(); m_cStatesApplied = false; }
    if (m_boostApplied) { RestoreMaxBoost(); m_boostApplied = false; }
    if (m_powerPlanApplied) { RestorePowerState(); m_powerPlanApplied = false; }
    if (m_dwmThreadsApplied) { RestoreDwmThreads(); m_dwmThreadsApplied = false; }
    const bool restored = m_snapshot.RestoreAll();
    m_restorePending = !restored;
    if (restored) {
        g.extremeActive = false;
        g.extremeWinlogonPids.clear();
        m_state.extremeActive.store(false);
        if (!m_stop.load()) ApplySystemToggles(cfg);
    }
    PublishProcessView();
}

bool BoosterCore::GetMemoryPriorityString(DWORD pid, wchar_t* buf, size_t len) const {
    if (!buf || len == 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD pc = GetPriorityClass(h);
    CloseHandle(h);
    const wchar_t* label = L"Normal";
    switch (pc) {
        case IDLE_PRIORITY_CLASS:        label = L"Idle"; break;
        case BELOW_NORMAL_PRIORITY_CLASS: label = L"BelowNormal"; break;
        case ABOVE_NORMAL_PRIORITY_CLASS: label = L"AboveNormal"; break;
        case HIGH_PRIORITY_CLASS:         label = L"High"; break;
        case REALTIME_PRIORITY_CLASS:     label = L"RealTime"; break;
    }
    wcsncpy_s(buf, len, label, _TRUNCATE);
    return true;
}

void BoosterCore::PublishProcessView() {
    std::wstring gameName;
    {
        std::lock_guard<std::mutex> nameLock(m_state.gameNameMutex);
        gameName = m_state.gameName;
    }
    std::set<DWORD> children(m_game.boostedChildren.begin(), m_game.boostedChildren.end());
    std::lock_guard<std::mutex> viewLock(m_viewMutex);
    m_viewGamePid = m_state.gamePid.load();
    m_viewGameName = std::move(gameName);
    m_viewThrottled = m_throttled;
    m_viewGameChildren = std::move(children);
}

void BoosterCore::GetActiveProcessList(std::vector<ActiveProcInfo>& out) {
    out.clear();
    DWORD gamePid = 0;
    std::wstring gameName;
    std::set<DWORD> throttled;
    {
        std::lock_guard<std::mutex> lk(m_viewMutex);
        gamePid = m_viewGamePid;
        gameName = m_viewGameName;
        throttled = m_viewThrottled;
    }
    if (gamePid && m_state.gameActive.load()) {
        ActiveProcInfo g;
        g.pid = gamePid;
        g.name = gameName;
        g.isGame = true;
        HANDLE h = OpenProcessForOpt(gamePid);
        if (h) {
            PriorityClass pc = PriorityClass::None;
            if (GetProcPriorityClass(h, &pc)) g.klass = PriorityClassName(pc);
            DWORD_PTR sys = 0, proc = 0;
            if (GetProcAffinity(h, &sys, &proc)) g.affinity = proc;
            CloseHandle(h);
        }
        if (g.klass.empty()) g.klass = L"high";
        out.push_back(g);
    }
    for (DWORD pid : throttled) {
        ActiveProcInfo p;
        p.pid = pid;
        p.name = GetProcessNameById(pid);
        p.throttled = true;
        DWORD_PTR sys = 0, proc = 0;
        HANDLE h = OpenProcessForOpt(pid);
        if (h) {
            if (GetProcAffinity(h, &sys, &proc)) p.affinity = proc;
            PriorityClass pc = PriorityClass::None;
            if (GetProcPriorityClass(h, &pc)) p.klass = PriorityClassName(pc);
            CloseHandle(h);
        } else {
            p.klass = L"Idle";
        }
        out.push_back(p);
    }
}

void BoosterCore::GetSysProcessList(std::vector<SysProcInfo>& out) {
    out.clear();
    ULONGLONG now = GetTickCount64();
    DWORD ncpu = std::max<DWORD>(1, m_cpuCoreCount ? m_cpuCoreCount : 1);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    DWORD gamePid = 0;
    std::set<DWORD> children;
    std::set<DWORD> throttled;
    {
        std::lock_guard<std::mutex> lk(m_viewMutex);
        gamePid = m_viewGamePid;
        children = m_viewGameChildren;
        throttled = m_viewThrottled;
    }

    std::map<DWORD, DWORD> ppid;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do { ppid[pe.th32ProcessID] = pe.th32ParentProcessID; } while (Process32NextW(snap, &pe));
    }

    std::map<DWORD, ULONGLONG> current;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == 0) continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!h) continue;
            FILETIME c, e, k, u;
            ULONGLONG total = 0;
            ULONGLONG creationTime = 0;
            if (GetProcessTimes(h, &c, &e, &k, &u)) {
                creationTime = ((ULONGLONG)c.dwHighDateTime << 32) | c.dwLowDateTime;
                ULONGLONG ku = ((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime;
                ULONGLONG uu = ((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime;
                total = ku + uu;
            }
            SysProcInfo p;
            p.pid = pe.th32ProcessID;
            p.creationTime = creationTime;
            p.name = WLower(pe.szExeFile);
            p.isGame = (p.pid == gamePid);
            p.throttled = throttled.count(p.pid) != 0;
            p.gameChild = children.count(p.pid) != 0 ||
                          (gamePid && ppid.count(p.pid) && ppid[p.pid] == gamePid);
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            pmc.cb = sizeof(pmc);
            if (GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
                p.ramMB = (DWORD)(pmc.WorkingSetSize / (1024 * 1024));
            DWORD_PTR sys = 0, aff = 0;
            if (GetProcessAffinityMask(h, &sys, &aff)) p.affinity = aff;
            DWORD pc = GetPriorityClass(h);
            switch (pc) {
                case IDLE_PRIORITY_CLASS:         p.priority = 4;  p.klass = L"Idle"; break;
                case BELOW_NORMAL_PRIORITY_CLASS: p.priority = 5;  p.klass = L"BelowNormal"; break;
                case NORMAL_PRIORITY_CLASS:       p.priority = 8;  p.klass = L"Normal"; break;
                case ABOVE_NORMAL_PRIORITY_CLASS: p.priority = 10; p.klass = L"AboveNormal"; break;
                case HIGH_PRIORITY_CLASS:         p.priority = 13; p.klass = L"High"; break;
                case REALTIME_PRIORITY_CLASS:      p.priority = 24; p.klass = L"RealTime"; break;
                default:                          p.priority = 0;  p.klass = L"?"; break;
            }
            CloseHandle(h);
            if (total) current[p.pid] = total;
            out.push_back(std::move(p));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (m_lastSampleTick && now > m_lastSampleTick) {
        double windowMs = (double)(now - m_lastSampleTick);
        for (auto& p : out) {
            auto cur = current.find(p.pid);
            auto prev = m_lastCpuTime.find(p.pid);
            if (cur != current.end() && prev != m_lastCpuTime.end() && cur->second > prev->second) {
                double delta100ns = (double)(cur->second - prev->second);
                double busyMs = delta100ns / 10000.0;
                p.cpuPct = busyMs / windowMs / (double)ncpu * 100.0;
                if (p.cpuPct < 0) p.cpuPct = 0;
                if (p.cpuPct > 100.0) p.cpuPct = 100.0;
            }
        }
    }
    m_lastCpuTime = std::move(current);
    m_lastSampleTick = now;

    std::sort(out.begin(), out.end(), [](const SysProcInfo& a, const SysProcInfo& b) {
        if (a.isGame != b.isGame) return a.isGame;
        if (a.cpuPct != b.cpuPct) return a.cpuPct > b.cpuPct;
        return a.ramMB > b.ramMB;
    });
}

void BoosterCore::ApplyGameTreeBoost(GameCtx& g, const Config& cfg) {
    if (!cfg.features.game_tree_boost || !g.pid) return;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    int boosted = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ParentProcessID != g.pid) continue;
            DWORD pid = pe.th32ProcessID;
            if (pid == g.pid || pid == GetCurrentProcessId()) continue;
            if (std::find(g.boostedChildren.begin(), g.boostedChildren.end(), pid) != g.boostedChildren.end())
                continue;
            std::wstring n = WLower(pe.szExeFile);
            if (IsSystemWhitelisted(n) || IsAntiCheatWhitelisted(n) || IsSecurityWhitelisted(n)) continue;
            if (IsProtectedByConfig(cfg, n)) continue;
            HANDLE h = OpenProcessForOpt(pid);
            if (!h) continue;
            PriorityClass before = PriorityClass::None;
            if (GetProcPriorityClass(h, &before)) g.boostedChildOrigPriority[pid] = before;
            bool ecoBefore = false;
            if (GetProcEcoQoS(h, &ecoBefore)) g.boostedChildOrigEco[pid] = ecoBefore;
            SetProcPriorityClass(h, PriorityClass::High);
            SetProcEcoQoS(h, false);
            CloseHandle(h);
            g.boostedChildren.push_back(pid);
            boosted++;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    m_state.boostedChildCount.store((DWORD)g.boostedChildren.size());
    if (boosted)
        Log(L"game", L"game-tree boost: %d helper process(es) of the game raised to High", boosted);
}

void BoosterCore::RevertGameTreeBoost(GameCtx& g) {
    for (DWORD pid : g.boostedChildren) {
        HANDLE h = OpenProcessForOpt(pid);
        if (h) {
            auto pri = g.boostedChildOrigPriority.find(pid);
            if (pri != g.boostedChildOrigPriority.end()) SetProcPriorityClass(h, pri->second);
            auto eco = g.boostedChildOrigEco.find(pid);
            if (eco != g.boostedChildOrigEco.end()) SetProcEcoQoS(h, eco->second);
            CloseHandle(h);
        }
    }
    if (!g.boostedChildren.empty())
        Log(L"game", L"game-tree boost reverted (%d process(es))", (int)g.boostedChildren.size());
    g.boostedChildren.clear();
    g.boostedChildOrigPriority.clear();
    g.boostedChildOrigEco.clear();
    m_state.boostedChildCount.store(0);
}

static void OptimizeGamingServices(BoosterState& st, GameCtx& game, SessionSnapshot& snapshot) {
    const ULONGLONG now = GetTickCount64();
    if (game.lastGamingServiceScan && now - game.lastGamingServiceScan < 10000) return;
    game.lastGamingServiceScan = now;
    std::vector<ServiceInfo> all;
    if (EnumServices(all) <= 0) return;
    int changed = 0;
    for (const auto& s : all) {
        if (!s.gamingTarget || s.protectedService || s.state != SERVICE_RUNNING) continue;
        const auto tracked = [&](const std::vector<std::wstring>& names) {
            for (const auto& name : names)
                if (_wcsicmp(name.c_str(), s.name) == 0) return true;
            return false;
        };
        if (tracked(game.pausedGamingServices) || tracked(game.stoppedGamingServices)) continue;
        const bool canPause = s.canPause && snapshot.CaptureServicePause(s.name);
        if (canPause && ServicePause(s.name)) {
            game.pausedGamingServices.emplace_back(s.name);
            changed++;
        } else if (s.canStop && snapshot.CaptureServiceStop(s.name)) {
            if (ServiceStop(s.name)) {
                game.stoppedGamingServices.emplace_back(s.name);
                changed++;
            }
        }
    }
    if (changed) {
        const DWORD total = (DWORD)(game.pausedGamingServices.size() + game.stoppedGamingServices.size());
        st.servicePauseCount.store(total);
        Log(L"service", L"%d optional service(s) optimized (%d paused, %d stopped; auto-restore enabled)",
            (int)total, (int)game.pausedGamingServices.size(), (int)game.stoppedGamingServices.size());
    }
}

static void RestoreGamingServices(BoosterState& st, GameCtx& game) {
    int restored = 0;
    const int expected = (int)(game.pausedGamingServices.size() + game.stoppedGamingServices.size());
    for (const auto& name : game.pausedGamingServices)
        if (ServiceResume(name)) restored++;
    for (const auto& name : game.stoppedGamingServices)
        if (ServiceStart(name)) restored++;
    game.pausedGamingServices.clear();
    game.stoppedGamingServices.clear();
    game.lastGamingServiceScan = 0;
    st.servicePauseCount.store(0);
    if (restored == expected) {
        Log(L"service", L"%d service(s) restored to their pre-game state", restored);
    } else {
        Log(L"service", L"restore incomplete: %d/%d service(s); Windows may retry trigger-start services",
            restored, expected);
    }
}

void BoosterCore::RestoreCurrentGame() {
    if (m_state.running.load()) m_restoreGameRequested.store(true);
}
