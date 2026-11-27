#include "etw.h"
#include "util.h"
#include <evntcons.h>
#include <string>

#ifndef PROCESS_TRACE_MODE_REAL_TIME
#define PROCESS_TRACE_MODE_REAL_TIME      0x00000100
#define PROCESS_TRACE_MODE_RAW_TIMESTAMP  0x00000200
#endif
#ifndef PROCESS_TRACE_MODE_EVENT_RECORD
#define PROCESS_TRACE_MODE_EVENT_RECORD   0x10000000
#endif

#pragma comment(lib, "advapi32.lib")

namespace kt {

static const GUID kKtSessionGuid = {0x2d69bc4e, 0x4f84, 0x4ad4, {0xa3, 0x87, 0x5b, 0x34, 0x8d, 0x1f, 0x72, 0xc1}};
static const GUID kProcessGuid = {0x3d6fa8d0, 0xfe05, 0x11d0, {0x95, 0xda, 0x00, 0xc0, 0x4f, 0xd9, 0x11, 0x34}};
static const GUID kThreadGuid  = {0x3d6fa8d1, 0xfe05, 0x11d0, {0x95, 0xda, 0x00, 0xc0, 0x4f, 0xd9, 0x11, 0x34}};

static const wchar_t kSessionName[] = L"KTGameBoosterETW";

void EtwMonitor::HandleEventRecord(EVENT_RECORD* rec) {
    InterlockedIncrement64(&m_events);
    if (rec->EventHeader.ProviderId == kThreadGuid) {
        if (!m_active || rec->EventHeader.EventDescriptor.Opcode != EVENT_TRACE_TYPE_START ||
            rec->UserDataLength < sizeof(DWORD) * 2) return;
        const DWORD* ids = static_cast<const DWORD*>(rec->UserData);
        DWORD pid = ids[0];
        DWORD tid = ids[1];
        if (!pid || !tid) return;
        EnterCriticalSection(&m_lock);
        m_newThreads.emplace_back(pid, tid);
        LeaveCriticalSection(&m_lock);
    }
    else if (rec->EventHeader.ProviderId == kProcessGuid) {
        if (!m_active) return;
        if (rec->EventHeader.EventDescriptor.Opcode != EVENT_TRACE_TYPE_START) return;

        const size_t pointerSize =
            (rec->EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER) ? 4u : 8u;
        if (rec->UserDataLength < pointerSize + sizeof(DWORD)) return;
        DWORD pid = 0;
        memcpy(&pid, static_cast<const BYTE*>(rec->UserData) + pointerSize, sizeof(pid));
        if (!pid) return;
        std::wstring exe = GetProcessNameById(pid);
        if (exe.empty()) return;
        EnterCriticalSection(&m_lock);
        m_newProcesses.emplace_back(pid, std::move(exe));
        LeaveCriticalSection(&m_lock);
    }
}

VOID WINAPI EtwMonitor::OnEventRecord(EVENT_RECORD* rec) {
    EtwMonitor* self = (EtwMonitor*)rec->UserContext;
    if (self) self->HandleEventRecord(rec);
}

DWORD WINAPI EtwMonitor::TraceThreadProc(LPVOID param) {
    EtwMonitor* self = (EtwMonitor*)param;
    if (!self) return 0;
    EVENT_TRACE_LOGFILE logfile;
    memset(&logfile, 0, sizeof(logfile));
    logfile.LoggerName = (LPWSTR)kSessionName;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME |
                               PROCESS_TRACE_MODE_RAW_TIMESTAMP |
                               PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = OnEventRecord;
    logfile.Context = self;

    self->m_trace = OpenTraceW(&logfile);
    if (self->m_trace == INVALID_PROCESSTRACE_HANDLE) {
        self->m_trace = 0;
        Log(L"etw", L"OpenTraceW failed");
        InterlockedExchange(&self->m_active, 0);
        return 1;
    }
    ProcessTrace(&self->m_trace, 1, 0, 0);
    return 0;
}

bool EtwMonitor::Start() {
    if (m_active) return true;
    InitializeCriticalSection(&m_lock);
    m_csInit = true;

    ULONG propsSize = (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) + sizeof(kSessionName));
    EVENT_TRACE_PROPERTIES* props = (EVENT_TRACE_PROPERTIES*)malloc(propsSize);
    if (!props) {
        DeleteCriticalSection(&m_lock);
        m_csInit = false;
        return false;
    }
    auto initializeProps = [&]() {
        memset(props, 0, propsSize);
        props->Wnode.BufferSize = propsSize;
        props->Wnode.Guid = kKtSessionGuid;
        props->Wnode.ClientContext = 1;
        props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        props->BufferSize = 64;
        props->MinimumBuffers = 16;
        props->MaximumBuffers = 128;
        props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE;
        props->EnableFlags = EVENT_TRACE_FLAG_PROCESS | EVENT_TRACE_FLAG_THREAD |
                             EVENT_TRACE_FLAG_NO_SYSCONFIG;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    };
    initializeProps();

    ULONG hr = StartTraceW(&m_session, (LPWSTR)kSessionName, props);
    if (hr != ERROR_SUCCESS) {
        if (hr == ERROR_ALREADY_EXISTS) {
            Log(L"etw", L"orphaned session detected, stopping...");
            ULONG stopResult = ControlTraceW(0, (LPWSTR)kSessionName, props,
                                             EVENT_TRACE_CONTROL_STOP);
            if (stopResult != ERROR_SUCCESS && stopResult != ERROR_WMI_INSTANCE_NOT_FOUND)
                Log(L"etw", L"orphan cleanup failed: code %08X", stopResult);
            m_session = 0;
            initializeProps();
            hr = StartTraceW(&m_session, (LPWSTR)kSessionName, props);
        }
        if (hr != ERROR_SUCCESS) {
            Log(L"etw", L"StartTraceW failed: code %08X", hr);
            m_session = 0;
            free(props);
            DeleteCriticalSection(&m_lock);
            m_csInit = false;
            return false;
        }
    }
    free(props);

    InterlockedExchange(&m_active, 1);
    m_thread = CreateThread(nullptr, 0, TraceThreadProc, this, 0, nullptr);
    if (!m_thread) {
        InterlockedExchange(&m_active, 0);
        if (m_session) {
            EVENT_TRACE_PROPERTIES stopProps = {};
            stopProps.Wnode.BufferSize = sizeof(stopProps);
            stopProps.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
            ControlTraceW(m_session, nullptr, &stopProps, EVENT_TRACE_CONTROL_STOP);
            m_session = 0;
        }
        DeleteCriticalSection(&m_lock);
        m_csInit = false;
        return false;
    }
    Log(L"etw", L"session 'KTGameBoosterETW' started");
    return true;
}

void EtwMonitor::Stop() {
    if (!m_active && !m_session && !m_trace && !m_thread && !m_csInit) return;
    InterlockedExchange(&m_active, 0);
    if (m_session) {
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
    if (m_csInit) {
        DeleteCriticalSection(&m_lock);
        m_csInit = false;
    }
}

std::vector<std::pair<DWORD, DWORD>> EtwMonitor::TakeNewThreads() {
    std::vector<std::pair<DWORD, DWORD>> out;
    if (!m_active) return out;
    EnterCriticalSection(&m_lock);
    out.swap(m_newThreads);
    LeaveCriticalSection(&m_lock);
    return out;
}

std::vector<std::pair<DWORD, std::wstring>> EtwMonitor::TakeNewProcesses() {
    std::vector<std::pair<DWORD, std::wstring>> out;
    if (!m_active) return out;
    EnterCriticalSection(&m_lock);
    out.swap(m_newProcesses);
    LeaveCriticalSection(&m_lock);
    return out;
}

}
