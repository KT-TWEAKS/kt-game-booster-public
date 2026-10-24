#include "procopt.h"
#include "util.h"
#include "snapshot.h"
#include <d3dkmthk.h>
#include <processthreadsapi.h>
#include <vector>
#include <algorithm>

namespace kt {

namespace {
struct CpuTopo {
    std::vector<std::vector<int>> ccds;
    std::vector<std::vector<int>> cores;
};

bool QueryTopology(CpuTopo& topo) {
    DWORD sz = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &sz);
    if (!sz) return false;
    std::vector<BYTE> buf(sz);
    if (!GetLogicalProcessorInformationEx(RelationCache,
                                          (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf.data(), &sz))
        return false;
    DWORD off = 0;
    while (off + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= sz) {
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX p =
            (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf.data() + off);
        if (p->Relationship == RelationCache && p->Cache.Level == 3 &&
            p->Cache.GroupMasks[0].Mask) {
            std::vector<int> procs;
            DWORD_PTR m = p->Cache.GroupMasks[0].Mask;
            for (int b = 0; b < 64; b++)
                if (m & ((DWORD_PTR)1 << b)) procs.push_back(b);
            topo.ccds.push_back(procs);
        }
        off += p->Size ? p->Size : sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
    }

    sz = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &sz);
    if (!sz) return !topo.ccds.empty();
    buf.assign(sz, 0);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
                                          (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf.data(), &sz))
        return !topo.ccds.empty();
    off = 0;
    while (off + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= sz) {
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX p =
            (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf.data() + off);
        if (p->Relationship == RelationProcessorCore && p->Processor.GroupCount > 0 &&
            p->Processor.GroupMask[0].Mask) {
            std::vector<int> procs;
            DWORD_PTR m = p->Processor.GroupMask[0].Mask;
            for (int b = 0; b < 64; b++)
                if (m & ((DWORD_PTR)1 << b)) procs.push_back(b);
            topo.cores.push_back(procs);
        }
        off += p->Size ? p->Size : sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
    }
    return !topo.ccds.empty();
}

bool SameCore(int a, int b, const CpuTopo& topo) {
    for (const auto& c : topo.cores) {
        bool ha = std::find(c.begin(), c.end(), a) != c.end();
        bool hb = std::find(c.begin(), c.end(), b) != c.end();
        if (ha && hb) return true;
    }
    return false;
}
}

bool BuildTopologyMask(int ccd, bool smt, DWORD_PTR sysMask, DWORD_PTR* outMask, int* ideal) {
    if (outMask) *outMask = 0;
    CpuTopo topo;
    if (!QueryTopology(topo) || topo.ccds.empty()) return false;

    std::vector<int> selected;
    int n = (int)topo.ccds.size();
    if (ccd >= 0 && ccd < n) {
        selected = topo.ccds[ccd];
    } else if (ccd == -1) {
        for (const auto& c : topo.ccds)
            selected.insert(selected.end(), c.begin(), c.end());
    } else {
        return false;
    }
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());

    if (!smt) {
        std::vector<int> keep;
        for (int p : selected) {
            bool dup = false;
            for (int k : keep)
                if (SameCore(p, k, topo)) { dup = true; break; }
            if (!dup) keep.push_back(p);
        }
        selected = keep;
    }

    DWORD_PTR mask = 0;
    for (int p : selected)
        if (p >= 0 && p < 64) mask |= ((DWORD_PTR)1 << p);
    mask &= sysMask;
    if (!mask) return false;
    if (outMask) *outMask = mask;
    if (ideal) {
        for (int p : selected)
            if (p >= 0 && p < 64 && (sysMask & ((DWORD_PTR)1 << p))) { *ideal = p; break; }
    }
    return true;
}

HANDLE OpenProcessForOpt(DWORD pid) {
    return OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION |
                       PROCESS_SET_QUOTA | PROCESS_SUSPEND_RESUME, FALSE, pid);
}

DWORD PriorityClassToWin(PriorityClass pc) {
    switch (pc) {
        case PriorityClass::Idle:        return IDLE_PRIORITY_CLASS;
        case PriorityClass::BelowNormal: return BELOW_NORMAL_PRIORITY_CLASS;
        case PriorityClass::Normal:      return NORMAL_PRIORITY_CLASS;
        case PriorityClass::AboveNormal: return ABOVE_NORMAL_PRIORITY_CLASS;
        case PriorityClass::High:        return HIGH_PRIORITY_CLASS;
        case PriorityClass::Realtime:    return REALTIME_PRIORITY_CLASS;
        default: return NORMAL_PRIORITY_CLASS;
    }
}

const wchar_t* PriorityClassName(PriorityClass pc) {
    switch (pc) {
        case PriorityClass::Idle: return L"idle";
        case PriorityClass::BelowNormal: return L"below_normal";
        case PriorityClass::Normal: return L"normal";
        case PriorityClass::AboveNormal: return L"above_normal";
        case PriorityClass::High: return L"high";
        case PriorityClass::Realtime: return L"realtime";
        default: return L"none";
    }
}

bool SetProcPriorityClass(HANDLE h, PriorityClass pc) {
    return SetPriorityClass(h, PriorityClassToWin(pc)) != FALSE;
}

bool GetProcPriorityClass(HANDLE h, PriorityClass* out) {
    DWORD pc = GetPriorityClass(h);
    if (pc == 0) return false;
    switch (pc) {
        case IDLE_PRIORITY_CLASS:        *out = PriorityClass::Idle; break;
        case BELOW_NORMAL_PRIORITY_CLASS:*out = PriorityClass::BelowNormal; break;
        case NORMAL_PRIORITY_CLASS:      *out = PriorityClass::Normal; break;
        case ABOVE_NORMAL_PRIORITY_CLASS:*out = PriorityClass::AboveNormal; break;
        case HIGH_PRIORITY_CLASS:        *out = PriorityClass::High; break;
        case REALTIME_PRIORITY_CLASS:    *out = PriorityClass::Realtime; break;
        default: *out = PriorityClass::Normal; break;
    }
    return true;
}

bool SuspendProcess(HANDLE h) {
    const NtApi& nt = GetNtApi();
    if (!nt.NtSuspendProcess) return false;
    return nt.NtSuspendProcess(h) == 0;
}

bool ResumeProcess(HANDLE h) {
    const NtApi& nt = GetNtApi();
    if (!nt.NtResumeProcess) return false;
    return nt.NtResumeProcess(h) == 0;
}

bool SetProcAffinity(HANDLE h, DWORD_PTR mask) {
    DWORD_PTR sys = 0, proc = 0;
    if (!GetProcessAffinityMask(h, &sys, &proc)) return false;
    return SetProcessAffinityMask(h, mask & sys) != FALSE;
}

bool GetProcAffinity(HANDLE h, DWORD_PTR* sys, DWORD_PTR* proc) {
    return GetProcessAffinityMask(h, sys, proc) != FALSE;
}

bool TrimWorkingSet(HANDLE h) {
    SetProcessWorkingSetSize(h, (SIZE_T)-1, (SIZE_T)-1);
    return true;
}

bool SetProcEcoQoS(HANDLE h, bool on) {
    PROCESS_POWER_THROTTLING_STATE st{};
    st.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    st.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    st.StateMask = on ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
    return SetProcessInformation(h, ProcessPowerThrottling, &st, sizeof(st)) != FALSE;
}

bool GetProcEcoQoS(HANDLE h, bool* out) {
    if (!out) return false;
    PROCESS_POWER_THROTTLING_STATE st{};
    st.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    if (!GetProcessInformation(h, ProcessPowerThrottling, &st, sizeof(st))) return false;
    *out = (st.ControlMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) != 0 &&
           (st.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) != 0;
    return true;
}

bool SetProcIoPriority(HANDLE h, int priority) {
    const NtApi& nt = GetNtApi();
    if (!nt.NtSetInformationProcess) return false;
    return nt.NtSetInformationProcess(h, Pr_ProcessIoPriority, &priority, sizeof(priority)) == 0;
}

bool GetProcIoPriority(HANDLE h, int* out) {
    if (!out) return false;
    const NtApi& nt = GetNtApi();
    if (!nt.NtQueryInformationProcess) return false;
    ULONG value = 0;
    if (nt.NtQueryInformationProcess(h, Pr_ProcessIoPriority, &value, sizeof(value), nullptr) != 0)
        return false;
    *out = (int)value;
    return true;
}

bool GetGpuSchedulingPriority(HANDLE h, int* outPriority) {
    if (!h || !outPriority) return false;
    D3DKMT_SCHEDULINGPRIORITYCLASS priority = D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL;
    if (D3DKMTGetProcessSchedulingPriorityClass(h, &priority) != 0) return false;
    *outPriority = static_cast<int>(priority);
    return true;
}

bool SetGpuSchedulingPriority(HANDLE h, int priority) {
    if (!h || priority < D3DKMT_SCHEDULINGPRIORITYCLASS_IDLE ||
        priority > D3DKMT_SCHEDULINGPRIORITYCLASS_REALTIME) return false;
    return D3DKMTSetProcessSchedulingPriorityClass(
        h, static_cast<D3DKMT_SCHEDULINGPRIORITYCLASS>(priority)) == 0;
}

bool SetGpuPriority(HANDLE h, bool high) {
    return SetGpuSchedulingPriority(h, high ? D3DKMT_SCHEDULINGPRIORITYCLASS_HIGH
                                             : D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL);
}

bool SetProcMemoryPriority(HANDLE h, int priority) {
    if (priority < 1 || priority > 5) return false;
    MEMORY_PRIORITY_INFORMATION mpi;
    mpi.MemoryPriority = (ULONG)priority;
    return SetProcessInformation(h, ProcessMemoryPriority, &mpi, sizeof(mpi)) != FALSE;
}

bool GetProcMemoryPriority(HANDLE h, int* out) {
    if (!out) return false;
    MEMORY_PRIORITY_INFORMATION mpi{};
    if (!GetProcessInformation(h, ProcessMemoryPriority, &mpi, sizeof(mpi))) return false;
    *out = (int)mpi.MemoryPriority;
    return true;
}

bool DisableMemoryCompression(SessionSnapshot& snapshot) {

    std::vector<DWORD> compPids;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring name = WLower(pe.szExeFile);
            if (name == L"memory compression.exe") {
                compPids.push_back(pe.th32ProcessID);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (compPids.empty()) return false;

    bool changed = false;
    for (DWORD pid : compPids) {
        HANDLE h = OpenProcessForOpt(pid);
        if (h) {
            if (snapshot.CaptureProcess(h, pid)) {
                const bool priorityChanged = SetProcPriorityClass(h, PriorityClass::BelowNormal);
                const bool memoryChanged = SetProcMemoryPriority(h, 1);
                changed = changed || priorityChanged || memoryChanged;
            }
            CloseHandle(h);
        }
    }
    return changed;
}

void RestoreMemoryCompression(SessionSnapshot& snapshot) {

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring name = WLower(pe.szExeFile);
            if (name == L"memory compression.exe") {
                snapshot.RestoreProcessForPid(pe.th32ProcessID);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

bool IsHwGpuSchedulingEnabled() {
    HKEY hKey;
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"HwSchMode", nullptr, nullptr,
                        (LPBYTE)&value, &size);
        RegCloseKey(hKey);
    }
    return value == 2;
}

bool SetHwGpuScheduling(bool enable, SessionSnapshot& snapshot) {
    if (!snapshot.CaptureRegistryDword(2,
        L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode"))
        return false;
    HKEY hKey;
    DWORD value = enable ? 2 : 1;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        LONG res = RegSetValueExW(hKey, L"HwSchMode", 0, REG_DWORD,
                                  (const BYTE*)&value, sizeof(value));
        RegCloseKey(hKey);
        return res == ERROR_SUCCESS;
    }
    return false;
}

}
