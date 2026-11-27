#include "cpulim.h"
#include "util.h"
#include "threadopt.h"
#include "snapshot.h"
#include <tlhelp32.h>

namespace kt {

void Cpulim::Configure(bool enabled, int mainFramePct, int workerPct) {
    m_enabled = enabled;
    m_mainFramePct = mainFramePct > 0 ? mainFramePct : 20;
    m_workerPct = workerPct > 0 ? workerPct : 3;
}

Cpulim::LimThread* Cpulim::FindByTid(DWORD tid) {
    for (auto& t : m_threads)
        if (t.tid == tid) return &t;
    return nullptr;
}

int Cpulim::QuotaFor(const LimThread& t) const {
    return t.isMain ? m_mainFramePct : m_workerPct;
}

void Cpulim::AddProcess(DWORD pid) {
    if (!m_enabled) return;
    auto it = m_pidRefs.find(pid);
    if (it != m_pidRefs.end()) { it->second++; return; }
    m_pidRefs[pid] = 1;

    FILETIME procCreate = {};
    bool haveProcCreate = false;
    {
        HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hp) {
            FILETIME e, k, u;
            if (GetProcessTimes(hp, &procCreate, &e, &k, &u)) {
                haveProcCreate = true;
            }
            CloseHandle(hp);
        }
    }
    bool mainFound = false;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            if (te.th32ThreadID == GetCurrentThreadId()) continue;
            HANDLE h = OpenThread(THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!h) continue;
            LimThread t;
            t.pid = pid;
            t.tid = te.th32ThreadID;
            t.handle = h;
            if (!CaptureThreadInfo(h, pid, te.th32ThreadID, &t.baseline)) {
                CloseHandle(h);
                continue;
            }
            t.isMain = false;
            if (haveProcCreate) {
                FILETIME e, k, u, tcreate;
                if (GetThreadTimes(h, &tcreate, &e, &k, &u) &&
                    tcreate.dwLowDateTime == procCreate.dwLowDateTime &&
                    tcreate.dwHighDateTime == procCreate.dwHighDateTime) {
                    t.isMain = true;
                    mainFound = true;
                }
            }
            m_threads.push_back(t);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    if (!mainFound) {
        LimThread* best = nullptr;
        for (auto& t : m_threads)
            if (t.pid == pid && (!best || t.tid < best->tid)) best = &t;
        if (best) best->isMain = true;
    }
}

void Cpulim::RemoveProcess(DWORD pid, SessionSnapshot* snapshot) {
    auto it = m_pidRefs.find(pid);
    if (it != m_pidRefs.end() && --it->second > 0) return;
    m_pidRefs.erase(pid);

    for (auto it2 = m_threads.begin(); it2 != m_threads.end();) {
        if (it2->pid == pid) {
            if (it2->suspended) {
                LONG prev = 0;
                if (ResumeThreadSafe(it2->handle, &prev)) {
                    if (snapshot) snapshot->SetThreadSuspensionPending(it2->baseline, false);
                    if (m_suspended) m_suspended--;
                }
            }
            CloseHandle(it2->handle);
            it2 = m_threads.erase(it2);
        } else {
            ++it2;
        }
    }
}

void Cpulim::Clear(SessionSnapshot* snapshot) {
    for (auto& t : m_threads) {
        if (t.suspended) {
            LONG prev = 0;
            if (ResumeThreadSafe(t.handle, &prev) && snapshot)
                snapshot->SetThreadSuspensionPending(t.baseline, false);
        }
        CloseHandle(t.handle);
    }
    m_threads.clear();
    m_pidRefs.clear();
    m_suspended = 0;
}

void Cpulim::Tick(DWORD intervalMs, SessionSnapshot* snapshot) {
    if (!m_enabled) return;
    ULONGLONG nowTick = GetTickCount64();
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER now = { nowFt.dwLowDateTime, nowFt.dwHighDateTime };

    for (auto it = m_threads.begin(); it != m_threads.end();) {
        LimThread& t = *it;

        if (t.suspended && nowTick >= t.resumeAt) {
            LONG prev = 0;
            if (ResumeThreadSafe(t.handle, &prev)) {
                t.suspended = false;
                if (snapshot) snapshot->SetThreadSuspensionPending(t.baseline, false);
                if (m_suspended) m_suspended--;
            }
        }
        if (t.suspended) { ++it; continue; }

        FILETIME create, exit, kft, uft;
        if (!GetThreadTimes(t.handle, &create, &exit, &kft, &uft)) {
            CloseHandle(t.handle);
            it = m_threads.erase(it);
            continue;
        }
        ULARGE_INTEGER k = { kft.dwLowDateTime, kft.dwHighDateTime };
        ULARGE_INTEGER u = { uft.dwLowDateTime, uft.dwHighDateTime };
        if (!t.has) {
            t.k = k; t.u = u; t.now = now;
            t.has = true;
            ++it;
            continue;
        }
        ULONGLONG dt = (now.QuadPart - t.now.QuadPart) ? (now.QuadPart - t.now.QuadPart) : 1;
        ULONGLONG busy = (k.QuadPart - t.k.QuadPart) + (u.QuadPart - t.u.QuadPart);
        double usage = (double)busy * 100.0 / (double)dt;
        t.k = k; t.u = u; t.now = now;

        int quota = QuotaFor(t);
        if (usage > quota) {
            double overFrac = (usage - quota) / usage;
            if (overFrac >= 0.1) {
                ULONGLONG suspendMs = (ULONGLONG)(intervalMs * overFrac);
                if (suspendMs < 8) suspendMs = 8;
                LONG prev = 0;
                const bool journaled = !snapshot ||
                    (snapshot->CaptureThread(t.baseline) &&
                     snapshot->SetThreadSuspensionPending(t.baseline, true));
                if (journaled && SuspendThreadSafe(t.handle, &prev)) {
                    if (prev > 0) {
                        LONG ignored = 0;
                        if (ResumeThreadSafe(t.handle, &ignored)) {
                            if (snapshot) snapshot->SetThreadSuspensionPending(t.baseline, false);
                        } else {
                            t.suspended = true;
                            t.resumeAt = nowTick;
                            m_suspended++;
                        }
                        ++it;
                        continue;
                    }
                    t.suspended = true;
                    t.resumeAt = nowTick + suspendMs;
                    m_suspended++;
                } else if (snapshot && journaled) {
                    snapshot->SetThreadSuspensionPending(t.baseline, false);
                }
            }
        }
        ++it;
    }
}

}
