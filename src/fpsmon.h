#pragma once
#ifndef KT_FPSMON_H
#define KT_FPSMON_H

#include <windows.h>
#include <evntrace.h>

namespace kt {

struct FpsStats {
    double    fps = 0.0;
    double    p95FrameMs = 0.0;
    double    p99FrameMs = 0.0;
    DWORD     samples = 0;
    ULONGLONG totalFrames = 0;
    bool      valid = false;
};

class FpsMonitor {
public:
    FpsMonitor() = default;
    ~FpsMonitor() { Stop(); }

    FpsMonitor(const FpsMonitor&) = delete;
    FpsMonitor& operator=(const FpsMonitor&) = delete;

    bool Start(DWORD gamePid);
    void Stop();
    bool IsRunning() const { return m_active != 0; }
    DWORD GamePid() const { return m_gamePid; }

    bool GetStats(FpsStats* out) const;

private:
    static VOID WINAPI OnEventRecordStatic(EVENT_RECORD* rec);
    void OnEventRecord(EVENT_RECORD* rec);
    static DWORD WINAPI TraceThreadProc(LPVOID param);

    void HandlePresent(EVENT_RECORD* rec);

    volatile LONG m_active = 0;
    DWORD m_gamePid = 0;
    int   m_pidOffset = -1;
    TRACEHANDLE m_session = 0;
    TRACEHANDLE m_trace = 0;
    HANDLE m_thread = nullptr;
    bool   m_csInit = false;
    mutable CRITICAL_SECTION m_lock{};

    static const size_t kRingCap = 512;
    double  m_ring[kRingCap] = {};
    size_t  m_ringCount = 0;
    size_t  m_ringHead = 0;
    ULONGLONG m_lastPresentTs = 0;
    ULONGLONG m_lastEventTick = 0;
    ULONGLONG m_totalFrames = 0;
    double  m_windowSumMs = 0.0;
};

}

#endif
