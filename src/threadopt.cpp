#include "threadopt.h"
#include "util.h"
#include "snapshot.h"

namespace kt {

int ThreadPrioToWin(ThreadPrio p) {
    switch (p) {
        case ThreadPrio::TimeCritical: return THREAD_PRIORITY_TIME_CRITICAL;
        case ThreadPrio::Highest:      return THREAD_PRIORITY_HIGHEST;
        case ThreadPrio::AboveNormal:  return THREAD_PRIORITY_ABOVE_NORMAL;
        case ThreadPrio::Normal:       return THREAD_PRIORITY_NORMAL;
        case ThreadPrio::BelowNormal:  return THREAD_PRIORITY_BELOW_NORMAL;
        case ThreadPrio::Lowest:       return THREAD_PRIORITY_LOWEST;
        case ThreadPrio::Idle:         return THREAD_PRIORITY_IDLE;
        default: return THREAD_PRIORITY_NORMAL;
    }
}

static int ActionToThreadPrio(Action a) {
    switch (a) {
        case Action::Idle:         return THREAD_PRIORITY_IDLE;
        case Action::BelowNormal:  return THREAD_PRIORITY_BELOW_NORMAL;
        case Action::Lowest:       return THREAD_PRIORITY_LOWEST;
        default: return -1;
    }
}

static ULONGLONG ThreadFileTime(const FILETIME& ft) {
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

bool CaptureThreadInfo(HANDLE hThread, DWORD pid, DWORD tid, ThreadInfo* out) {
    if (!hThread || !pid || !tid || !out) return false;
    ThreadInfo state{};
    state.pid = pid;
    state.tid = tid;
    state.handle = hThread;

    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(hThread, &created, &exited, &kernel, &user)) return false;
    state.creationTime = ThreadFileTime(created);
    if (!state.creationTime || GetProcessIdOfThread(hThread) != pid) return false;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    FILETIME processCreated{};
    const bool haveProcessTime = GetProcessTimes(process, &processCreated, &exited, &kernel, &user) != FALSE;
    CloseHandle(process);
    if (!haveProcessTime) return false;
    state.processCreationTime = ThreadFileTime(processCreated);
    if (!state.processCreationTime) return false;

    state.name = GetThreadName(hThread);
    state.basePriority = GetThreadPriority(hThread);
    state.currentPriority = state.basePriority;
    state.priorityKnown = state.basePriority != THREAD_PRIORITY_ERROR_RETURN;

    BOOL boostDisabled = FALSE;
    if (GetThreadPriorityBoost(hThread, &boostDisabled)) {
        state.priorityBoostKnown = true;
        state.priorityBoostDisabled = boostDisabled != FALSE;
    }
    state.idealProcessorKnown = GetThreadIdealProcessorEx(hThread, &state.idealProcessor) != FALSE;

    const NtApi& nt = GetNtApi();
    ULONG returned = 0;
    BYTE critical = 0;
    if (nt.NtQueryInformationThread &&
        nt.NtQueryInformationThread(hThread, Th_ThreadMmCritical, &critical,
                                    sizeof(critical), &returned) == 0) {
        state.mmCriticalKnown = true;
        state.mmCritical = critical != 0;
    }
    returned = 0;
    if (nt.NtQueryInformationThread &&
        nt.NtQueryInformationThread(hThread, Th_ThreadPagePriority, &state.pagePriority,
                                    sizeof(state.pagePriority), &returned) == 0)
        state.pagePriorityKnown = true;
    returned = 0;
    if (nt.NtQueryInformationThread &&
        nt.NtQueryInformationThread(hThread, Th_ThreadIoPriority, &state.ioPriority,
                                    sizeof(state.ioPriority), &returned) == 0)
        state.ioPriorityKnown = true;

    THREAD_POWER_THROTTLING_STATE_EX power{};
    if (GetThreadInformation(hThread, ThreadPowerThrottling, &power, sizeof(power))) {
        state.powerThrottleKnown = true;
        state.powerThrottle.controlMask = power.ControlMask;
        state.powerThrottle.stateMask = power.StateMask;
    }
    *out = std::move(state);
    return true;
}

bool RestoreThreadInfo(HANDLE hThread, const ThreadInfo& state) {
    if (!hThread) return false;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(hThread, &created, &exited, &kernel, &user) ||
        ThreadFileTime(created) != state.creationTime || GetProcessIdOfThread(hThread) != state.pid) {
        return true;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, state.pid);
    if (!process) return false;
    FILETIME processCreated{};
    const bool sameProcess = GetProcessTimes(process, &processCreated, &exited, &kernel, &user) &&
        ThreadFileTime(processCreated) == state.processCreationTime;
    CloseHandle(process);
    if (!sameProcess) return true;

    bool ok = true;
    if (state.priorityKnown) ok = SetThreadPriority(hThread, state.basePriority) != FALSE && ok;
    if (state.priorityBoostKnown)
        ok = SetThreadPriorityBoost(hThread, state.priorityBoostDisabled ? TRUE : FALSE) != FALSE && ok;
    if (state.idealProcessorKnown) {
        PROCESSOR_NUMBER ideal = state.idealProcessor;
        ok = SetThreadIdealProcessorEx(hThread, &ideal, nullptr) != FALSE && ok;
    }
    if (state.mmCriticalKnown) ok = SetThreadMmCritical(hThread, state.mmCritical) && ok;
    if (state.pagePriorityKnown) ok = SetThreadPagePriority(hThread, (int)state.pagePriority) && ok;
    if (state.ioPriorityKnown) ok = SetThreadIoPriority(hThread, (int)state.ioPriority) && ok;
    if (state.powerThrottleKnown) {
        THREAD_POWER_THROTTLING_STATE_EX power{};
        power.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION_EX;
        power.ControlMask = state.powerThrottle.controlMask;
        power.StateMask = state.powerThrottle.stateMask;
        ok = SetThreadInformation(hThread, ThreadPowerThrottling, &power, sizeof(power)) != FALSE && ok;
    }
    return ok;
}

std::vector<ThreadInfo> EnumerateThreads(DWORD pid) {
    std::vector<ThreadInfo> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;

    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            ThreadInfo ti;
            ti.tid = te.th32ThreadID;
            ti.pid = pid;
            if (te.th32ThreadID == GetCurrentThreadId()) {
                ti.suspendable = false;
                out.push_back(ti);
                continue;
            }
            HANDLE h = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION |
                                  THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (h) {
                if (!CaptureThreadInfo(h, pid, te.th32ThreadID, &ti)) {
                    CloseHandle(h);
                    ti.handle = nullptr;
                }
            }
            out.push_back(ti);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return out;
}

#ifndef THREAD_POWER_THROTTLING_EXECUTION_REQUIRED
#define THREAD_POWER_THROTTLING_EXECUTION_REQUIRED 0x1
#endif
static bool SetThreadPowerThrottle(HANDLE h, bool throttle) {
    THREAD_POWER_THROTTLING_STATE_EX st{};
    st.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION_EX;
    st.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED |
                     THREAD_POWER_THROTTLING_EXECUTION_REQUIRED;
    st.StateMask = throttle ? 0
                            : (THREAD_POWER_THROTTLING_EXECUTION_SPEED |
                               THREAD_POWER_THROTTLING_EXECUTION_REQUIRED);
    return SetThreadInformation(h, ThreadPowerThrottling, &st, sizeof(st)) != FALSE;
}

static bool SetThreadIoPriority(HANDLE h, int priority) {
    const NtApi& nt = GetNtApi();
    if (!nt.NtSetInformationThread) return false;
    return nt.NtSetInformationThread(h, Th_ThreadIoPriority, &priority, sizeof(priority)) == 0;
}

static bool SuspendThreadNt(HANDLE h, LONG* out) {
    const NtApi& nt = GetNtApi();
    if (!nt.NtSuspendThread) return false;
    ULONG prev = 0;
    LONG st = nt.NtSuspendThread(h, &prev);
    if (out) *out = (LONG)prev;
    return st == 0;
}

static bool ResumeThreadNt(HANDLE h, LONG* out) {
    const NtApi& nt = GetNtApi();
    if (!nt.NtResumeThread) return false;
    ULONG prev = 0;
    LONG st = nt.NtResumeThread(h, &prev);
    if (out) *out = (LONG)prev;
    return st == 0;
}

bool SuspendThreadSafe(HANDLE hThread, LONG* outCount) { return SuspendThreadNt(hThread, outCount); }
bool ResumeThreadSafe(HANDLE hThread, LONG* outCount) { return ResumeThreadNt(hThread, outCount); }

bool SuspendProcessThreads(DWORD pid, SessionSnapshot& snapshot) {
    auto threads = EnumerateThreads(pid);
    bool suspendedAny = false;
    for (auto& thread : threads) {
        if (!thread.handle || !thread.suspendable ||
            !snapshot.CaptureThread(thread) ||
            !snapshot.SetThreadSuspensionPending(thread, true))
            continue;
        LONG previous = 0;
        if (!SuspendThreadSafe(thread.handle, &previous)) {
            snapshot.SetThreadSuspensionPending(thread, false);
        } else if (previous > 0) {
            LONG ignored = 0;
            if (ResumeThreadSafe(thread.handle, &ignored)) {
                snapshot.SetThreadSuspensionPending(thread, false);
            } else {
                thread.suspensionAdded = true;
                suspendedAny = true;
            }
        } else {
            thread.suspensionAdded = true;
            suspendedAny = true;
        }
    }
    for (auto& thread : threads) if (thread.handle) CloseHandle(thread.handle);
    return suspendedAny;
}

bool SetThreadMmCritical(HANDLE hThread, bool critical) {
    const NtApi& nt = GetNtApi();
    if (!nt.NtSetInformationThread) return false;
    BYTE value = critical ? 1 : 0;
    return nt.NtSetInformationThread(hThread, Th_ThreadMmCritical, &value, sizeof(value)) == 0;
}

bool SetThreadPagePriority(HANDLE hThread, int priority) {
    const NtApi& nt = GetNtApi();
    if (!nt.NtSetInformationThread) return false;
    ULONG value = (ULONG)priority;
    return nt.NtSetInformationThread(hThread, Th_ThreadPagePriority, &value, sizeof(value)) == 0;
}

void ApplyThreadRule(ThreadInfo& thread, const ThreadRule& rule,
                     const Features& feat, bool isGame, SessionSnapshot* snapshot) {
    HANDLE hThread = thread.handle;
    if (!hThread || (snapshot && !snapshot->CaptureThread(thread))) return;
    if (isGame) {
        if (thread.priorityKnown && rule.priority != ThreadPrio::None) {
            SetThreadPriority(hThread, ThreadPrioToWin(rule.priority));
        }
        if (thread.powerThrottleKnown && feat.thread_power_game_unthrottle) {
            SetThreadPowerThrottle(hThread, false);
        }
        if (thread.priorityBoostKnown) SetThreadPriorityBoost(hThread, FALSE);
        if (feat.boost_input_threads && thread.priorityKnown &&
            (thread.name.find(L"Input") != std::wstring::npos ||
             thread.name.find(L"RawInput") != std::wstring::npos)) {
            SetThreadPriority(hThread, THREAD_PRIORITY_TIME_CRITICAL);
        }
        if (thread.mmCriticalKnown && feat.mmcss_game) {
            SetThreadMmCritical(hThread, true);
        }
        if (thread.pagePriorityKnown && feat.page_priority_game) {
            SetThreadPagePriority(hThread, 7);
        }
        if (thread.ioPriorityKnown && feat.io_priority_game_high) {
            SetThreadIoPriority(hThread, 6);
        }
    } else {
        int tp = ActionToThreadPrio(rule.action);
        if (thread.priorityKnown && tp != -1) SetThreadPriority(hThread, tp);
        if (rule.action == Action::Suspend) {
            bool marked = snapshot && snapshot->SetThreadSuspensionPending(thread, true);
            if (marked) {
                LONG prev = 0;
                if (!SuspendThreadSafe(hThread, &prev)) {
                    snapshot->SetThreadSuspensionPending(thread, false);
                } else if (prev > 0) {
                    LONG ignored = 0;
                    if (ResumeThreadSafe(hThread, &ignored)) {
                        snapshot->SetThreadSuspensionPending(thread, false);
                    } else {
                        thread.suspensionAdded = true;
                    }
                } else {
                    thread.suspensionAdded = true;
                }
            }
        }
        if (thread.priorityKnown && rule.priority != ThreadPrio::None) {
            SetThreadPriority(hThread, ThreadPrioToWin(rule.priority));
        }
        if (thread.powerThrottleKnown && feat.thread_power_background_throttle) {
            SetThreadPowerThrottle(hThread, true);
        }
        if (thread.ioPriorityKnown && feat.io_priority_background) {
            SetThreadIoPriority(hThread, 2);
        }
        if (thread.powerThrottleKnown && feat.eco_qos_background) {
            SetThreadPowerThrottle(hThread, true);
        }
        if (thread.priorityBoostKnown && feat.disable_priority_boost) {
            SetThreadPriorityBoost(hThread, TRUE);
        }
    }
}

void OptimizeGameThreads(DWORD gamePid, const Config& cfg, SessionSnapshot* snapshot) {
    auto threads = EnumerateThreads(gamePid);
    int optimized = 0;
    for (auto& t : threads) {
        if (!t.handle || (snapshot && !snapshot->CaptureThread(t))) continue;
        bool applied = false;
        for (const auto& rule : cfg.threads) {
            if (WildMatch(rule.name.c_str(), t.name.c_str())) {
                ApplyThreadRule(t, rule, cfg.features, true, snapshot);
                applied = true;
            }
        }
        if (!applied) {
            if (t.powerThrottleKnown && cfg.features.thread_power_game_unthrottle)
                SetThreadPowerThrottle(t.handle, false);
            if (t.pagePriorityKnown && cfg.features.page_priority_game)
                SetThreadPagePriority(t.handle, 7);
            if (t.ioPriorityKnown && cfg.features.io_priority_game_high)
                SetThreadIoPriority(t.handle, 6);
        }
        if (applied) optimized++;
    }
    for (auto& t : threads) if (t.handle) CloseHandle(t.handle);
    if (optimized) Log(L"threads", L"%d threads optimized for PID %u", optimized, gamePid);
}

void ThrottleBackgroundThreads(DWORD bgPid, const Config& cfg, SessionSnapshot* snapshot) {
    auto threads = EnumerateThreads(bgPid);
    for (auto& t : threads) {
        if (!t.handle || !t.suspendable || (snapshot && !snapshot->CaptureThread(t))) continue;
        bool matched = false;
        for (const auto& rule : cfg.threads) {
            if (rule.action == Action::None) continue;
            if (WildMatch(rule.name.c_str(), t.name.c_str())) {
                ApplyThreadRule(t, rule, cfg.features, false, snapshot);
                matched = true;
            }
        }
        if (!matched) {
            ThreadRule generic;
            ApplyThreadRule(t, generic, cfg.features, false, snapshot);
        }
    }
    for (auto& t : threads) if (t.handle) CloseHandle(t.handle);
}

void RestoreThreads(DWORD, const std::vector<ThreadInfo>& saved) {
    for (const auto& t : saved) {
        HANDLE h = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION |
                              THREAD_SUSPEND_RESUME, FALSE, t.tid);
        if (!h) continue;
        if (RestoreThreadInfo(h, t) && t.suspensionAdded) {
            LONG prev = 0;
            ResumeThreadSafe(h, &prev);
        }
        CloseHandle(h);
    }
}

void RestoreBackgroundThreads(DWORD pid, SessionSnapshot& snapshot) {
    snapshot.RestoreProcessThreads(pid);
}

}
