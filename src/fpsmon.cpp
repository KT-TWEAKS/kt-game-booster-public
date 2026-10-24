#include "fpsmon.h"
#include "util.h"
#include <evntcons.h>
#include <algorithm>
#include <cstring>
#include <vector>

#ifndef PROCESS_TRACE_MODE_REAL_TIME
#define PROCESS_TRACE_MODE_REAL_TIME     0x00000100
#endif
#ifndef PROCESS_TRACE_MODE_EVENT_RECORD
#define PROCESS_TRACE_MODE_EVENT_RECORD  0x10000000
#endif

#pragma comment(lib, "advapi32.lib")

namespace kt {

static const GUID kDxgKrnlGuid =
    { 0x8c416c79, 0xd49b, 0x4d01, { 0x84, 0x18, 0xd0, 0xc9, 0x8b, 0x84, 0x72, 0xf1 } };

static const USHORT kPresentEventId = 73;

static const wchar_t kSessionName[] = L"KTGameBoosterPresent";

VOID WINAPI FpsMonitor::OnEventRecordStatic(EVENT_RECORD* rec) {
    FpsMonitor* self = (FpsMonitor*)rec->UserContext;
    if (self) self->OnEventRecord(rec);
}

DWORD WINAPI FpsMonitor::TraceThreadProc(LPVOID param) {
    FpsMonitor* self = (FpsMonitor*)param;
    if (!self) return 0;

    EVENT_TRACE_LOGFILEW logfile;
    memset(&logfile, 0, sizeof(logfile));
    logfile.LoggerName = (LPWSTR)kSessionName;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = OnEventRecordStatic;
    logfile.Context = self;

    self->m_trace = OpenTraceW(&logfile);
    if (self->m_trace == INVALID_PROCESSTRACE_HANDLE) {
        self->m_trace = 0;
        Log(L"fps", L"OpenTraceW failed for the Present session");
        InterlockedExchange(&self->m_active, 0);
        return 1;
    }
    ProcessTrace(&self->m_trace, 1, nullptr, nullptr);
    return 0;
}

void FpsMonitor::OnEventRecord(EVENT_RECORD* rec) {
    if (!m_active) return;
    if (rec->EventHeader.ProviderId != kDxgKrnlGuid) return;
    if (rec->EventHeader.EventDescriptor.Id != kPresentEventId) return;
    HandlePresent(rec);
}

void FpsMonitor::HandlePresent(EVENT_RECORD* rec) {
    const ULONG len = rec->UserDataLength;
    const BYTE* data = (const BYTE*)rec->UserData;
    if (!data || len < 4) return;
    if (m_gamePid == 0) return;

    bool hit = false;
    int foundAt = -1;
    for (ULONG off = 0; off + 4 <= len; off++) {
        DWORD v;
        memcpy(&v, data + off, 4);
        if (v == m_gamePid) { foundAt = (int)off; break; }
    }
    if (m_pidOffset >= 0 && m_pidOffset + 4 <= (int)len) {
        DWORD v;
        memcpy(&v, data + m_pidOffset, 4);
        if (v == m_gamePid) {
            hit = true;
        } else if (foundAt >= 0) {
            m_pidOffset = foundAt;
            hit = true;
        } else {
            m_pidOffset = -1;
        }
    } else if (foundAt >= 0) {
        m_pidOffset = foundAt;
        hit = true;
    }

    if (!hit) return;

    const ULONGLONG ts = (ULONGLONG)rec->EventHeader.TimeStamp.QuadPart;
    EnterCriticalSection(&m_lock);
    if (m_lastPresentTs && ts > m_lastPresentTs) {
        const ULONGLONG dt100ns = ts - m_lastPresentTs;
        const double frameMs = (double)dt100ns / 10000.0;
        if (frameMs >= 0.5 && frameMs <= 2000.0) {
            if (m_ringCount == kRingCap) {
                m_windowSumMs -= m_ring[m_ringHead];
                m_ringHead = (m_ringHead + 1) % kRingCap;
                m_ringCount--;
            }
            const size_t slot = (m_ringHead + m_ringCount) % kRingCap;
            m_ring[slot] = frameMs;
            m_ringCount++;
            m_windowSumMs += frameMs;
            m_totalFrames++;
            m_lastEventTick = GetTickCount64();
        }
    }
    m_lastPresentTs = ts;
    LeaveCriticalSection(&m_lock);
}

bool FpsMonitor::Start(DWORD gamePid) {
    if (gamePid == 0) return false;
    if (m_active) {
        if (m_gamePid == gamePid) return true;
        Stop();
    }

    if (!m_csInit) {
        InitializeCriticalSection(&m_lock);
        m_csInit = true;
    }
    m_gamePid = gamePid;
    m_pidOffset = -1;
    m_lastPresentTs = 0;
    m_lastEventTick = 0;
    m_ringCount = 0;
    m_ringHead = 0;
    m_windowSumMs = 0.0;
    m_totalFrames = 0;

    ULONG propsSize = (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) + sizeof(kSessionName));
    EVENT_TRACE_PROPERTIES* props = (EVENT_TRACE_PROPERTIES*)malloc(propsSize);
    if (!props) return false;
    auto initProps = [&]() {
        memset(props, 0, propsSize);
        props->Wnode.BufferSize = propsSize;
        props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        props->Wnode.ClientContext = 1;
        props->BufferSize = 64;
        props->MinimumBuffers = 16;
        props->MaximumBuffers = 64;
        props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    };
    initProps();

    ULONG hr = StartTraceW(&m_session, (LPWSTR)kSessionName, props);
    if (hr == ERROR_ALREADY_EXISTS) {
        Log(L"fps", L"orphaned Present session detected, stopping it");
        ControlTraceW(0, (LPWSTR)kSessionName, props, EVENT_TRACE_CONTROL_STOP);
        m_session = 0;
        initProps();
        hr = StartTraceW(&m_session, (LPWSTR)kSessionName, props);
    }
    if (hr != ERROR_SUCCESS) {
        Log(L"fps", L"StartTraceW failed: code %08X (FPS unavailable)", hr);
        free(props);
        m_gamePid = 0;
        return false;
    }

    ULONG er = EnableTraceEx2(m_session, &kDxgKrnlGuid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                              TRACE_LEVEL_INFORMATION, 0, 0, 0, nullptr);
    if (er != ERROR_SUCCESS) {
        Log(L"fps", L"EnableTraceEx2(DxgKrnl) failed: code %08X (FPS unavailable)", er);
        EVENT_TRACE_PROPERTIES stopProps;
        memset(&stopProps, 0, sizeof(stopProps));
        stopProps.Wnode.BufferSize = sizeof(stopProps);
        stopProps.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        ControlTraceW(m_session, (LPWSTR)kSessionName, &stopProps, EVENT_TRACE_CONTROL_STOP);
        m_session = 0;
        free(props);
        m_gamePid = 0;
        return false;
    }
    free(props);

    InterlockedExchange(&m_active, 1);
    m_thread = CreateThread(nullptr, 0, TraceThreadProc, this, 0, nullptr);
    if (!m_thread) {
        InterlockedExchange(&m_active, 0);
        if (m_session) {
            EVENT_TRACE_PROPERTIES stopProps;
            memset(&stopProps, 0, sizeof(stopProps));
            stopProps.Wnode.BufferSize = sizeof(stopProps);
            stopProps.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
            ControlTraceW(m_session, (LPWSTR)kSessionName, &stopProps, EVENT_TRACE_CONTROL_STOP);
            m_session = 0;
        }
        m_gamePid = 0;
        return false;
    }
    Log(L"fps", L"Present monitor active for PID %lu", gamePid);
    return true;
}

void FpsMonitor::Stop() {
    if (!m_active && !m_session && !m_trace && !m_thread) {
        m_gamePid = 0;
        return;
    }
    InterlockedExchange(&m_active, 0);

    if (m_session) {
        EnableTraceEx2(m_session, &kDxgKrnlGuid, EVENT_CONTROL_CODE_DISABLE_PROVIDER,
                       TRACE_LEVEL_INFORMATION, 0, 0, 0, nullptr);
        ULONG propsSize = (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) + sizeof(kSessionName));
        EVENT_TRACE_PROPERTIES* props = (EVENT_TRACE_PROPERTIES*)malloc(propsSize);
        if (props) {
            memset(props, 0, propsSize);
            props->Wnode.BufferSize = propsSize;
            props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
            ControlTraceW(m_session, (LPWSTR)kSessionName, props, EVENT_TRACE_CONTROL_STOP);
            free(props);
        }
        m_session = 0;
    }
    if (m_trace) {
        TRACEHANDLE t = m_trace;
        m_trace = 0;
        CloseTrace(t);
    }
    if (m_thread) {
        WaitForSingleObject(m_thread, 2000);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }
    m_lastPresentTs = 0;
    m_totalFrames = 0;
    m_gamePid = 0;
}

bool FpsMonitor::GetStats(FpsStats* out) const {
    if (!out) return false;
    *out = FpsStats{};
    if (!m_active) return false;

    FpsStats r;
    EnterCriticalSection(&m_lock);
    r.totalFrames = m_totalFrames;
    r.samples = (DWORD)m_ringCount;
    if (m_ringCount > 0 && m_windowSumMs > 0) {
        r.fps = (double)m_ringCount * 1000.0 / m_windowSumMs;

        std::vector<double> sorted;
        sorted.reserve(m_ringCount);
        for (size_t i = 0; i < m_ringCount; i++)
            sorted.push_back(m_ring[(m_ringHead + i) % kRingCap]);
        std::sort(sorted.begin(), sorted.end());
        size_t n = sorted.size();
        r.p95FrameMs = sorted[(size_t)((n - 1) * 0.95)];
        r.p99FrameMs = sorted[(size_t)((n - 1) * 0.99)];
    }
    const ULONGLONG lastTick = m_lastEventTick;
    LeaveCriticalSection(&m_lock);

    if (r.samples >= 15 && lastTick && GetTickCount64() - lastTick <= 1500) {
        r.valid = true;
        *out = r;
        return true;
    }
    *out = r;
    return false;
}

}
