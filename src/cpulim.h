#pragma once
#ifndef KT_CPULIM_H
#define KT_CPULIM_H

#include <windows.h>
#include <map>
#include <vector>
#include "threadopt.h"

namespace kt {

class SessionSnapshot;

class Cpulim {
public:
    void Configure(bool enabled, int mainFramePct, int workerPct);
    void AddProcess(DWORD pid);
    void RemoveProcess(DWORD pid, SessionSnapshot* snapshot = nullptr);
    void Clear(SessionSnapshot* snapshot = nullptr);
    void Tick(DWORD intervalMs, SessionSnapshot* snapshot = nullptr);
    size_t TrackedThreadCount() const { return m_threads.size(); }
    DWORD  CurrentSuspended() const { return m_suspended; }

private:
    struct LimThread {
        DWORD pid = 0;
        DWORD tid = 0;
        HANDLE handle = nullptr;
        ThreadInfo baseline;
        ULARGE_INTEGER k{}, u{}, now{};
        bool has = false;
        bool isMain = false;
        bool suspended = false;
        ULONGLONG resumeAt = 0;
    };

    bool  m_enabled = true;
    int   m_mainFramePct = 20;
    int   m_workerPct = 3;
    DWORD m_suspended = 0;
    std::vector<LimThread> m_threads;
    std::map<DWORD, int> m_pidRefs;

    LimThread* FindByTid(DWORD tid);
    int QuotaFor(const LimThread& t) const;
};

}

#endif
