#pragma once
#ifndef KT_ENGINE_API_H
#define KT_ENGINE_API_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KT_API __declspec(dllexport)

KT_API int  KT_Initialize();
KT_API void KT_Shutdown();
KT_API int  KT_Start();
KT_API void KT_Stop();
KT_API void KT_SetSafeMode(int enabled);
KT_API int  KT_RecoverPendingSession();
KT_API void KT_SetPaused(int paused);
KT_API void KT_SetTournament(int on);
KT_API void KT_SetTestTarget(const wchar_t* exe);
KT_API int  KT_SetFeature(const char* name, int value);
KT_API int  KT_IsElevated();
KT_API void KT_RestoreShell();

typedef struct KtState {
    int         running;
    int         paused;
    int         gameActive;
    DWORD       gamePid;
    wchar_t     gameName[64];
    DWORD       bgCount;
    DWORD       suspendedThreads;
    ULONGLONG   memFreed;
    ULONGLONG   etwEvents;
    DWORD       totalRuns;
    DWORD       totalGameSessions;
    ULONGLONG   totalGameSeconds;
    DWORD       uptimeSec;
    DWORD       gameUptimeSec;
    double      sysCpu;
    ULONGLONG   sysRamUsedMB;
    ULONGLONG   sysRamTotalMB;
    int         procCount;
    int         tournament;
    int         detectionConfidence;
    int         extremeActive;
    int         featureMask;
    int         autoCleanCount;
    int         servicePauseCount;
    int         boostedChildCount;
    int         peakCpuPctX100;
    int         peakRamPctX100;
    int         elevated;
    int         etwActive;
    int         fpsX100;
    int         frameP95us;
    int         frameP99us;
    int         frameSamples;
    int         featureMask2;
    int         gpuPctX100;
    wchar_t     gpuName[64];
    int         anticheatSafe;
    int         anticheatKind;
    wchar_t     gameProfile[32];
} KtState;
KT_API void KT_GetState(KtState* out);

typedef struct KtProc {
    DWORD     pid;
    int       isGame;
    int       throttled;
    wchar_t   klass[32];
    wchar_t   name[64];
    ULONGLONG affinity;
} KtProc;
KT_API int  KT_GetProcesses(KtProc* out, int maxCount);

typedef struct KtSysProc {
    DWORD     pid;
    int       isGame;
    int       throttled;
    int       gameChild;
    double    cpuPct;
    DWORD     ramMB;
    ULONGLONG affinity;
    int       priority;
    wchar_t   name[64];
    wchar_t   klass[32];
    ULONGLONG creationTime;
    double    gpuPct;
} KtSysProc;
KT_API int  KT_GetSysProcesses(KtSysProc* out, int maxCount);

KT_API int  KT_GovernProcess(DWORD pid, int action, ULONGLONG value);
KT_API int  KT_GovernProcessEx(DWORD pid, ULONGLONG expectedCreationTime,
                               int action, ULONGLONG value);

typedef struct KtService {
    wchar_t   name[64];
    wchar_t   displayName[96];
    DWORD     state;
    DWORD     pid;
    int       canStop;
    int       canPause;
    int       gamingTarget;
    int       protectedService;
} KtService;
KT_API int  KT_GetServices(KtService* out, int maxCount);
KT_API int  KT_ServiceControl(const wchar_t* name, int action);

KT_API int  KT_AddExclusion(const wchar_t* exe);
KT_API int  KT_RemoveExclusion(const wchar_t* exe);

KT_API int  KT_AddGame(const wchar_t* exe);

KT_API void KT_LoadProfile(const wchar_t* name);
KT_API int  KT_ListProfiles(wchar_t* out, int maxCount, int nameLen);

KT_API ULONGLONG KT_CleanRam();

typedef struct KtLogLine {
    int       level;
    wchar_t   text[512];
} KtLogLine;
KT_API int  KT_GetLogV2(int minLevel, int maxLines, KtLogLine* out);

typedef struct KtMetricsSample {
    double    cpuPct;
    ULONGLONG ramUsedMB;
    ULONGLONG ramTotalMB;
    DWORD     fps;
    DWORD     pingMs;
    ULONGLONG tickMs;
    double    gpuPct;
} KtMetricsSample;
KT_API int  KT_GetMetricsHistory(KtMetricsSample* out, int maxCount);

typedef struct KtBenchResult {
    DWORD     latencyUsAvg;
    DWORD     latencyUsP50;
    DWORD     latencyUsP95;
    DWORD     latencyUsMax;
    DWORD     samples;
    DWORD     durationMs;
    ULONGLONG tickMs;
    int       valid;
} KtBenchResult;
KT_API int  KT_RunBench(unsigned int durationMs, KtBenchResult* out);

KT_API void KT_SetConfigPath(const wchar_t* path);

#ifdef __cplusplus
}
#endif

#endif
