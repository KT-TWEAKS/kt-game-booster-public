#pragma once
#ifndef KT_BOOSTER_H
#define KT_BOOSTER_H

#include <windows.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <map>
#include <set>
#include <vector>
#include <string>
#include "config.h"
#include "fpsmon.h"
#include "threadopt.h"
#include "cpulim.h"
#include "etw.h"
#include "driver.h"
#include "snapshot.h"

namespace kt {

struct BoosterOptions {
    bool dry_run = false;
    bool no_background = false;
    bool no_eco_qos = false;
    bool no_io_priority = false;
    bool no_thread_power = false;
    DWORD poll_interval_ms = 1000;
};

struct ActiveProcInfo {
    DWORD pid = 0;
    std::wstring name;
    std::wstring klass;
    bool isGame = false;
    DWORD_PTR affinity = 0;
    bool throttled = false;
};

struct SysProcInfo {
    DWORD pid = 0;
    std::wstring name;
    std::wstring klass;
    bool isGame = false;
    bool gameChild = false;
    bool throttled = false;
    double cpuPct = 0.0;
    DWORD ramMB = 0;
    DWORD_PTR affinity = 0;
    int priority = 0;
    ULONGLONG creationTime = 0;
};

struct BoosterState {
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> gameActive{false};
    std::atomic<DWORD> gamePid{0};
    std::atomic<DWORD> suspendedThreads{0};
    std::atomic<DWORD> bgCount{0};
    std::atomic<DWORD> totalRuns{0};
    std::atomic<ULONGLONG> etwEvents{0};
    std::atomic<ULONGLONG> memFreed{0};
    std::atomic<ULONGLONG> startedTick{0};
    std::atomic<ULONGLONG> gameStartedTick{0};
    std::atomic<DWORD> totalGameSessions{0};
    std::atomic<ULONGLONG> totalGameSeconds{0};
    std::atomic<int> detectionConfidence{0};
    std::atomic<bool> extremeActive{false};
    std::atomic<DWORD> autoCleanCount{0};
    std::atomic<DWORD> servicePauseCount{0};
    std::atomic<DWORD> boostedChildCount{0};
    std::atomic<DWORD> peakCpuPct{0};
    std::atomic<DWORD> peakRamPct{0};
    std::atomic<DWORD> fpsX100{0};
    std::atomic<DWORD> frameP95Us{0};
    std::atomic<DWORD> frameP99Us{0};
    std::atomic<DWORD> frameSamples{0};
    std::atomic<bool> anticheatSafe{false};
    std::atomic<int> anticheatKind{0};
    mutable std::mutex gameProfileMutex;
    wchar_t gameProfile[32]{};
    mutable std::mutex cfgMutex;
    Config cfg;
    mutable std::mutex gameNameMutex;
    wchar_t gameName[64]{};
};

struct GameCtx {
    DWORD pid = 0;
    std::wstring name;
    HANDLE h = nullptr;
    PriorityClass origPriority = PriorityClass::None;
    PriorityClass appliedPriority = PriorityClass::None;
    DWORD_PTR origSys = 0, origProc = 0;
    std::vector<ThreadInfo> savedThreads;
    std::vector<ProcessRule> matchedRules;
    bool ioHighApplied = false;
    bool memPrioHighApplied = false;
    bool origIoKnown = false;
    int origIoPriority = 2;
    bool origMemKnown = false;
    int origMemPriority = 5;
    bool origEcoKnown = false;
    bool origEcoQoS = false;
    bool gpuPriorityApplied = false;
    bool gpuPriorityRestoreWarned = false;
    std::vector<DWORD> gameInputPids;
    bool memoryCompressionDisabled = false;
    bool gamingPlanApplied = false;
    bool extremeActive = false;
    std::vector<DWORD> extremeWinlogonPids;
    std::vector<DWORD> boostedChildren;
    std::map<DWORD, PriorityClass> boostedChildOrigPriority;
    std::map<DWORD, bool> boostedChildOrigEco;
    std::vector<std::wstring> pausedGamingServices;
    std::vector<std::wstring> stoppedGamingServices;
    ULONGLONG lastGamingServiceScan = 0;
    bool gameDvrApplied = false;
};

class BoosterCore {
public:
    BoosterCore();
    ~BoosterCore();

    void Start(const Config& cfg, const BoosterOptions& opt);
    void Stop();
    void SetPaused(bool p);
    bool IsPaused() const { return m_state.paused.load(); }
    bool IsRunning() const { return m_state.running.load(); }
    bool IsEtwActive() const { return m_etw.Active(); }
    void SetSafeMode(bool enabled) { m_safeMode.store(enabled); }
    bool IsSafeMode() const { return m_safeMode.load(); }
    bool RecoverPendingSession() { return m_snapshot.RecoverPendingJournal(); }
    Config GetConfigSnapshot() const {
        std::lock_guard<std::mutex> lk(m_state.cfgMutex);
        return m_state.cfg;
    }

    void UpdateConfig(const Config& cfg);

    ULONGLONG ForceCleanRam();

    DWORD CpuCoreCount() const { return m_cpuCoreCount; }

    bool GetMemoryPriorityString(DWORD pid, wchar_t* buf, size_t len) const;

    void GetActiveProcessList(std::vector<ActiveProcInfo>& out);

    void GetSysProcessList(std::vector<SysProcInfo>& out);

    void SetTestTarget(const std::wstring& exe);
    void ClearTestTarget() { SetTestTarget(L""); }

    void SetTournament(bool on);

    void RestoreShellNow();

    void StoreGameProfile(const std::wstring& name);
    void ClearGameProfile() { StoreGameProfile(L""); }

    void RestoreCurrentGame();

    void SetConfigSource(std::function<Config()> get) { m_cfgGet = std::move(get); }

    BoosterState& State() { return m_state; }

private:
    static DWORD WINAPI WorkerMain(LPVOID p);
    void Run();

    void ApplyGameTreeBoost(GameCtx& g, const Config& cfg);
    void RevertGameTreeBoost(GameCtx& g);

    void ReloadConfigIfChanged();
    void ApplySystemToggles(const Config& cfg);
    void ApplyGameOptimizations(GameCtx& g, const Config& cfg, const BoosterOptions& opt);
    void CollectBackgroundPids(const GameCtx& g, const Config& cfg, std::vector<DWORD>& bg);
    void ApplyBackgroundLimits(const std::vector<DWORD>& bg, const Config& cfg,
                               const BoosterOptions& opt, std::set<DWORD>* throttled);
    void RefreshBackground(const GameCtx& g, const Config& cfg, const BoosterOptions& opt,
                           std::set<DWORD>* throttled);
    void RestoreGame(GameCtx& g, std::set<DWORD>& throttled);
    void PublishProcessView();
    void UnlimitBackground(DWORD pid, const Config& cfg, std::set<DWORD>* throttled);

    BoosterState m_state;
    BoosterOptions m_opt;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_safeMode{false};
    std::atomic<bool> m_restoreShellRequested{false};
    std::atomic<bool> m_restoreGameRequested{false};
    std::atomic<bool> m_cfgDirty{true};
    bool m_restorePending = false;
    bool m_extremeMarkerCleanupPending = false;
    std::function<Config()> m_cfgGet;
    FILETIME m_cfgMtime{};
    std::mutex m_testMutex;
    std::wstring m_testTarget;
    std::atomic<ULONGLONG> m_gameSecondsAccum{0};
    HANDLE m_thread = nullptr;
    Cpulim m_lim;
    SessionSnapshot m_snapshot;
    EtwMonitor m_etw;
    FpsMonitor m_fps;
    Driver m_driver;
    DWORD m_cpuCoreCount = 0;
    bool m_cStatesApplied = false;
    bool m_powerPlanApplied = false;
    bool m_boostApplied = false;
    bool m_dwmMmcssApplied = false;
    bool m_dwmThreadsApplied = false;
    std::map<DWORD, DWORD_PTR> m_bgOrigAffinity;
    std::map<DWORD, PriorityClass> m_bgOrigPriority;
    std::map<DWORD, int> m_bgOrigIoPriority;
    std::map<DWORD, int> m_bgOrigMemoryPriority;
    std::map<DWORD, bool> m_bgOrigEcoQoS;
    GameCtx m_game;
    std::set<DWORD> m_throttled;
    mutable std::mutex m_viewMutex;
    DWORD m_viewGamePid = 0;
    std::wstring m_viewGameName;
    std::set<DWORD> m_viewThrottled;
    std::set<DWORD> m_viewGameChildren;
    std::map<DWORD, ULONGLONG> m_lastCpuTime;
    ULONGLONG m_lastSampleTick = 0;
};

}

#endif
