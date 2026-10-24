#pragma once
#ifndef KT_ETW_H
#define KT_ETW_H

#include <windows.h>
#include <evntrace.h>
#include <vector>
#include <utility>
#include <string>

namespace kt {

class EtwMonitor {
public:
    bool Start();
    void Stop();
    bool Active() const { return m_active; }

    std::vector<std::pair<DWORD, DWORD>> TakeNewThreads();

    std::vector<std::pair<DWORD, std::wstring>> TakeNewProcesses();

    ULONGLONG EventCount() const { return (ULONGLONG)m_events; }

private:
    volatile LONG m_active = 0;
    volatile LONG64 m_events = 0;
    HANDLE m_thread = nullptr;
    TRACEHANDLE m_session = 0;
    TRACEHANDLE m_trace = 0;
    CRITICAL_SECTION m_lock;
    bool m_csInit = false;
    std::vector<std::pair<DWORD, DWORD>> m_newThreads;
    std::vector<std::pair<DWORD, std::wstring>> m_newProcesses;

    static VOID WINAPI OnEventRecord(EVENT_RECORD* rec);
    static DWORD WINAPI TraceThreadProc(LPVOID param);
    void HandleEventRecord(EVENT_RECORD* rec);
};

}

#endif
