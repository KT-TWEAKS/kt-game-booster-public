#pragma once
#ifndef KT_SNAPSHOT_H
#define KT_SNAPSHOT_H

#include <windows.h>
#include <map>
#include <string>
#include <vector>

#include "config.h"
#include "threadopt.h"

namespace kt {

class SessionSnapshot {
public:
    SessionSnapshot();

    bool RecoverPendingJournal();
    void Begin();

    bool CaptureProcess(HANDLE process, DWORD pid);
    bool HasGpuPrioritySnapshot(DWORD pid) const;
    bool GetGpuPrioritySnapshot(DWORD pid, int* outPriority) const;
    bool CaptureThreads(const std::vector<ThreadInfo>& threads);
    bool CaptureThread(const ThreadInfo& thread);
    bool SetThreadSuspensionPending(const ThreadInfo& thread, bool pending);
    bool RestoreProcessThreads(DWORD pid);
    bool RestoreProcessForPid(DWORD pid);
    bool CaptureServicePause(const std::wstring& name);
    bool CaptureServiceStop(const std::wstring& name);
    bool CapturePowerPlan();
    bool CapturePowerSetting(const GUID& scheme, const GUID& subgroup, const GUID& setting);

    bool CaptureExtremeActive();
    bool ClearExtremeActive();

    bool CaptureRegistryValue(int kind, const std::wstring& valueName,
                              bool existed, const std::wstring& raw);
    bool CaptureRegistryDword(int rootId, const std::wstring& keyPath,
                              const std::wstring& valueName);

    bool RestoreAll();
    bool HasPendingChanges() const;

private:
    struct ProcessState {
        DWORD pid = 0;
        ULONGLONG creationTime = 0;
        PriorityClass priority = PriorityClass::None;
        DWORD_PTR affinity = 0;
        bool ioKnown = false;
        int ioPriority = 2;
        bool memoryKnown = false;
        int memoryPriority = 5;
        bool ecoKnown = false;
        bool ecoQos = false;
        bool cpuSetsKnown = false;
        std::vector<DWORD> cpuSets;
        bool gpuPriorityKnown = false;
        int gpuPriority = 2;
    };

    struct ThreadKey {
        DWORD tid = 0;
        ULONGLONG creationTime = 0;
        bool operator<(const ThreadKey& other) const {
            return tid < other.tid || (tid == other.tid && creationTime < other.creationTime);
        }
    };

    struct PowerState {
        GUID scheme = GUID_NULL;
        GUID subgroup = GUID_NULL;
        GUID setting = GUID_NULL;
        DWORD acValue = 0;
    };

    struct RegistryState {
        int kind = 0;
        int rootId = 0;
        bool existed = false;
        std::wstring keyPath;
        std::wstring valueName;
        std::wstring raw;
    };

    std::wstring m_path;
    std::map<DWORD, ProcessState> m_processes;
    std::map<ThreadKey, ThreadInfo> m_threads;
    std::map<std::wstring, int> m_services;
    std::vector<RegistryState> m_registry;
    std::vector<PowerState> m_powerSettings;
    bool m_powerKnown = false;
    GUID m_powerScheme = GUID_NULL;
    bool m_extremeActive = false;
    bool m_legacyThreadRecords = false;

    bool RestoreRegistryRecord(const RegistryState& state) const;
    bool RestorePowerSetting(const PowerState& state) const;
    bool RestoreThreadRecord(ThreadInfo& state);

    bool LoadJournal();
    bool Persist() const;
    void ClearMemory();
    bool RestoreProcess(const ProcessState& state) const;
    static std::wstring DefaultJournalPath();
};

}

#endif
