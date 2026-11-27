#pragma once
#ifndef KT_THREADOPT_H
#define KT_THREADOPT_H

#include <windows.h>
#include <string>
#include <vector>
#include "config.h"

namespace kt {

class SessionSnapshot;

struct ThreadPowerState {
    DWORD controlMask = 0;
    DWORD stateMask = 0;
};

struct ThreadInfo {
    DWORD tid = 0;
    DWORD pid = 0;
    ULONGLONG creationTime = 0;
    ULONGLONG processCreationTime = 0;
    HANDLE handle = nullptr;
    std::wstring name;
    int  basePriority = 0;
    int  currentPriority = 0;
    bool priorityKnown = false;
    bool priorityBoostKnown = false;
    bool priorityBoostDisabled = false;
    bool idealProcessorKnown = false;
    PROCESSOR_NUMBER idealProcessor{};
    bool mmCriticalKnown = false;
    bool mmCritical = false;
    bool pagePriorityKnown = false;
    ULONG pagePriority = 5;
    bool ioPriorityKnown = false;
    ULONG ioPriority = 5;
    bool powerThrottleKnown = false;
    ThreadPowerState powerThrottle{};
    bool suspensionAdded = false;
    bool suspendable = true;
};

std::vector<ThreadInfo> EnumerateThreads(DWORD pid);
bool CaptureThreadInfo(HANDLE hThread, DWORD pid, DWORD tid, ThreadInfo* out);
bool RestoreThreadInfo(HANDLE hThread, const ThreadInfo& state);
bool SuspendProcessThreads(DWORD pid, SessionSnapshot& snapshot);

int ThreadPrioToWin(ThreadPrio p);

void ApplyThreadRule(ThreadInfo& thread, const ThreadRule& rule,
                     const Features& feat, bool isGame, SessionSnapshot* snapshot);

void OptimizeGameThreads(DWORD gamePid, const Config& cfg, SessionSnapshot* snapshot = nullptr);

void ThrottleBackgroundThreads(DWORD bgPid, const Config& cfg, SessionSnapshot* snapshot = nullptr);

void RestoreThreads(DWORD pid, const std::vector<ThreadInfo>& saved);

void RestoreBackgroundThreads(DWORD pid, SessionSnapshot& snapshot);

bool SuspendThreadSafe(HANDLE hThread, LONG* outCount);
bool ResumeThreadSafe(HANDLE hThread, LONG* outCount);

bool SetThreadMmCritical(HANDLE hThread, bool critical);

bool SetThreadPagePriority(HANDLE hThread, int priority);

bool SetThreadIoPriority(HANDLE hThread, int priority);

}

#endif
