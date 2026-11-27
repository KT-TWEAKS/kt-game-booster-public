#include "snapshot.h"

#include "powersys.h"
#include "procopt.h"
#include "util.h"
#include "winsvcmon.h"

#include <powrprof.h>

#include <fstream>
#include <sstream>

namespace kt {
namespace {

ULONGLONG FileTimeValue(const FILETIME& ft) {
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

bool ProcessStillMatches(HANDLE h, ULONGLONG expected) {
    if (!expected) return true;
    FILETIME created{}, exited{}, kernel{}, user{};
    return GetProcessTimes(h, &created, &exited, &kernel, &user) &&
           FileTimeValue(created) == expected;
}

std::wstring CpuSetsToText(const std::vector<DWORD>& ids) {
    std::wstringstream out;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i) out << L',';
        out << ids[i];
    }
    return out.str();
}

std::vector<DWORD> CpuSetsFromText(const std::wstring& text) {
    std::vector<DWORD> ids;
    if (text.empty() || text == L"-") return ids;
    std::wstringstream in(text);
    std::wstring item;
    while (std::getline(in, item, L',')) {
        if (!item.empty()) ids.push_back(static_cast<DWORD>(wcstoul(item.c_str(), nullptr, 10)));
    }
    return ids;
}

}

SessionSnapshot::SessionSnapshot() : m_path(DefaultJournalPath()) {}

std::wstring SessionSnapshot::DefaultJournalPath() {
    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    std::wstring root = (n && n < MAX_PATH) ? base : L".";
    root += L"\\KTGameBooster";
    if (!CreateDirectoryW(root.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        root = L".\\.kt-state";
        CreateDirectoryW(root.c_str(), nullptr);
    }
    return root + L"\\session.journal";
}

void SessionSnapshot::ClearMemory() {
    m_processes.clear();
    m_threads.clear();
    m_services.clear();
    m_registry.clear();
    m_powerSettings.clear();
    m_powerKnown = false;
    m_powerScheme = GUID_NULL;
    m_extremeActive = false;
    m_legacyThreadRecords = false;
}

void SessionSnapshot::Begin() {
    ClearMemory();
    DeleteFileW(m_path.c_str());
}

bool SessionSnapshot::HasPendingChanges() const {
    return !m_processes.empty() || !m_threads.empty() || !m_services.empty() ||
           !m_registry.empty() || !m_powerSettings.empty() || m_powerKnown ||
           m_extremeActive || m_legacyThreadRecords;
}

bool SessionSnapshot::CaptureRegistryValue(int kind, const std::wstring& valueName,
                                           bool existed, const std::wstring& raw) {
    RegistryState r;
    r.kind = kind;
    r.existed = existed;
    r.valueName = valueName;
    r.raw = raw;
    for (auto& existing : m_registry) {
        if (existing.kind == kind && existing.valueName == valueName) {
            return true;
        }
    }
    m_registry.push_back(r);
    if (!Persist()) {
        m_registry.pop_back();
        Log(L"snapshot", L"journal write failed for registry kind %d; tweak aborted", kind);
        return false;
    }
    return true;
}

bool SessionSnapshot::CaptureProcess(HANDLE process, DWORD pid) {
    if (!process || !pid) return false;
    ProcessState state;
    state.pid = pid;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return false;
    state.creationTime = FileTimeValue(created);
    if (!state.creationTime) return false;
    auto existing = m_processes.find(pid);
    const bool replacingReusedPid = existing != m_processes.end() &&
                                    existing->second.creationTime != state.creationTime;
    ProcessState previous{};
    if (existing != m_processes.end()) {
        if (!replacingReusedPid) return true;
        previous = existing->second;
        m_processes.erase(existing);
        Log(L"snapshot", L"PID %lu was reused; recording the new process identity", pid);
    }
    GetProcPriorityClass(process, &state.priority);
    DWORD_PTR systemMask = 0;
    GetProcAffinity(process, &systemMask, &state.affinity);
    state.ioKnown = GetProcIoPriority(process, &state.ioPriority);
    state.memoryKnown = GetProcMemoryPriority(process, &state.memoryPriority);
    state.ecoKnown = GetProcEcoQoS(process, &state.ecoQos);
    state.cpuSetsKnown = GetProcessCpuSets(process, &state.cpuSets);
    state.gpuPriorityKnown = GetGpuSchedulingPriority(process, &state.gpuPriority);
    m_processes[pid] = state;
    if (!Persist()) {
        m_processes.erase(pid);
        if (replacingReusedPid) m_processes[pid] = previous;
        Log(L"snapshot", L"journal write failed; process PID %lu will not be changed", pid);
        return false;
    }
    return true;
}

bool SessionSnapshot::CaptureThreads(const std::vector<ThreadInfo>& threads) {
    for (const auto& thread : threads)
        if (thread.handle && !CaptureThread(thread)) return false;
    return true;
}

bool SessionSnapshot::HasGpuPrioritySnapshot(DWORD pid) const {
    auto found = m_processes.find(pid);
    return found != m_processes.end() && found->second.gpuPriorityKnown;
}

bool SessionSnapshot::GetGpuPrioritySnapshot(DWORD pid, int* outPriority) const {
    if (!outPriority) return false;
    auto found = m_processes.find(pid);
    if (found == m_processes.end() || !found->second.gpuPriorityKnown) return false;
    *outPriority = found->second.gpuPriority;
    return true;
}

bool SessionSnapshot::CaptureRegistryDword(int rootId, const std::wstring& keyPath,
                                           const std::wstring& valueName) {
    if ((rootId != 1 && rootId != 2) || keyPath.empty() || valueName.empty()) return false;
    for (const auto& existing : m_registry)
        if (existing.kind == 4 && existing.rootId == rootId &&
            existing.keyPath == keyPath && existing.valueName == valueName) return true;
    HKEY root = rootId == 1 ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(root, keyPath.c_str(), 0, KEY_QUERY_VALUE, &key);
    bool existed = false;
    DWORD value = 0;
    if (status == ERROR_SUCCESS) {
        DWORD type = 0, size = sizeof(value);
        status = RegQueryValueExW(key, valueName.c_str(), nullptr, &type,
                                  reinterpret_cast<BYTE*>(&value), &size);
        RegCloseKey(key);
        if (status == ERROR_SUCCESS) {
            if (type != REG_DWORD || size != sizeof(value)) {
                Log(L"snapshot", L"unsupported registry type for %s", valueName.c_str());
                return false;
            }
            existed = true;
        } else if (status != ERROR_FILE_NOT_FOUND) {
            return false;
        }
    } else if (status != ERROR_FILE_NOT_FOUND) {
        Log(L"snapshot", L"cannot read registry key %s (error %ld)", keyPath.c_str(), status);
        return false;
    }

    RegistryState state;
    state.kind = 4;
    state.rootId = rootId;
    state.existed = existed;
    state.keyPath = keyPath;
    state.valueName = valueName;
    state.raw = std::to_wstring(value);
    m_registry.push_back(std::move(state));
    if (!Persist()) {
        m_registry.pop_back();
        Log(L"snapshot", L"journal write failed for registry value %s", valueName.c_str());
        return false;
    }
    return true;
}

bool SessionSnapshot::CaptureThread(const ThreadInfo& thread) {
    if (!thread.handle || !thread.tid || !thread.pid || !thread.creationTime ||
        !thread.processCreationTime) return false;
    const ThreadKey key{ thread.tid, thread.creationTime };
    if (m_threads.count(key)) return true;
    ThreadInfo baseline = thread;
    baseline.handle = nullptr;
    baseline.suspendable = false;
    baseline.suspensionAdded = false;
    m_threads.emplace(key, std::move(baseline));
    if (!Persist()) {
        m_threads.erase(key);
        Log(L"snapshot", L"journal write failed while capturing TID %lu", thread.tid);
        return false;
    }
    return true;
}

bool SessionSnapshot::SetThreadSuspensionPending(const ThreadInfo& thread, bool pending) {
    if (!CaptureThread(thread)) return false;
    const ThreadKey key{ thread.tid, thread.creationTime };
    auto found = m_threads.find(key);
    if (found == m_threads.end()) return false;
    const bool previous = found->second.suspensionAdded;
    if (previous == pending) return true;
    found->second.suspensionAdded = pending;
    if (!Persist()) {
        if (pending) found->second.suspensionAdded = previous;
        Log(L"snapshot", L"journal write failed while updating suspension state for TID %lu", thread.tid);
        return false;
    }
    return true;
}

bool SessionSnapshot::CaptureServicePause(const std::wstring& name) {
    if (name.empty() || m_services.count(name)) return !name.empty();
    m_services[name] = 1;
    if (!Persist()) {
        m_services.erase(name);
        Log(L"snapshot", L"journal write failed for service %s", name.c_str());
        return false;
    }
    return true;
}

bool SessionSnapshot::CaptureServiceStop(const std::wstring& name) {
    if (name.empty()) return false;
    auto existing = m_services.find(name);
    if (existing != m_services.end()) {
        if (existing->second == 2) return true;
        const int previous = existing->second;
        existing->second = 2;
        if (!Persist()) {
            existing->second = previous;
            Log(L"snapshot", L"journal write failed for service %s", name.c_str());
            return false;
        }
        return true;
    }
    m_services[name] = 2;
    if (!Persist()) {
        m_services.erase(name);
        Log(L"snapshot", L"journal write failed for service %s", name.c_str());
        return false;
    }
    return true;
}

bool SessionSnapshot::CapturePowerPlan() {
    if (m_powerKnown) return true;
    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active) {
        Log(L"snapshot", L"could not capture the active power plan");
        return false;
    }
    m_powerScheme = *active;
    LocalFree(active);
    m_powerKnown = true;
    if (!Persist()) {
        m_powerKnown = false;
        Log(L"snapshot", L"journal write failed; power plan will not be changed");
        return false;
    }
    return true;
}

bool SessionSnapshot::CapturePowerSetting(const GUID& scheme, const GUID& subgroup,
                                          const GUID& setting) {
    for (const auto& old : m_powerSettings)
        if (IsEqualGUID(old.scheme, scheme) && IsEqualGUID(old.subgroup, subgroup) &&
            IsEqualGUID(old.setting, setting)) return true;
    DWORD acValue = 0;
    if (PowerReadACValueIndex(nullptr, &scheme, &subgroup, &setting, &acValue) != ERROR_SUCCESS) {
        Log(L"snapshot", L"could not capture AC power setting");
        return false;
    }
    PowerState state;
    state.scheme = scheme;
    state.subgroup = subgroup;
    state.setting = setting;
    state.acValue = acValue;
    m_powerSettings.push_back(state);
    if (!Persist()) {
        m_powerSettings.pop_back();
        Log(L"snapshot", L"journal write failed for AC power setting");
        return false;
    }
    return true;
}

bool SessionSnapshot::CaptureExtremeActive() {
    if (m_extremeActive) return true;
    m_extremeActive = true;
    if (!Persist()) {
        m_extremeActive = false;
        Log(L"snapshot", L"journal write failed; extreme mode not activated");
        return false;
    }
    return true;
}

bool SessionSnapshot::ClearExtremeActive() {
    if (!m_extremeActive) return true;
    m_extremeActive = false;
    if (!HasPendingChanges()) {
        if (DeleteFileW(m_path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        m_extremeActive = true;
        return false;
    }
    if (Persist()) return true;
    m_extremeActive = true;
    return false;
}

bool SessionSnapshot::Persist() const {
    const size_t separator = m_path.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        const std::wstring folder = m_path.substr(0, separator);
        if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            Log(L"snapshot", L"cannot create journal directory %s (error %lu)", folder.c_str(), GetLastError());
            return false;
        }
    }
    std::wstring temp = m_path + L".tmp";
    std::wofstream out(temp, std::ios::trunc);
    if (!out.is_open()) {
        Log(L"snapshot", L"cannot open journal %s", temp.c_str());
        return false;
    }
    out << L"KT_SESSION_JOURNAL_V3\n";
    if (m_powerKnown) {
        wchar_t guid[64]{};
        StringFromGUID2(m_powerScheme, guid, 64);
        out << L"W " << guid << L"\n";
    }
    if (m_extremeActive) out << L"E\n";
    if (m_legacyThreadRecords) out << L"X legacy-thread-state\n";
    for (const auto& pair : m_processes) {
        const ProcessState& p = pair.second;
        const std::wstring cpuSetsText = CpuSetsToText(p.cpuSets);
        out << L"P " << p.pid << L' ' << p.creationTime << L' ' << static_cast<int>(p.priority)
            << L' ' << static_cast<ULONGLONG>(p.affinity) << L' ' << p.ioKnown << L' ' << p.ioPriority
            << L' ' << p.memoryKnown << L' ' << p.memoryPriority << L' ' << p.ecoKnown << L' ' << p.ecoQos
            << L' ' << p.cpuSetsKnown << L' ' << (cpuSetsText.empty() ? L"-" : cpuSetsText)
            << L' ' << p.gpuPriorityKnown << L' ' << p.gpuPriority << L"\n";
    }
    for (const auto& pair : m_threads) {
        const ThreadInfo& t = pair.second;
        out << L"T " << t.tid << L' ' << t.pid << L' ' << t.creationTime << L' '
            << t.processCreationTime << L' ' << t.basePriority << L' ' << t.priorityKnown << L' '
            << t.priorityBoostKnown << L' ' << t.priorityBoostDisabled << L' '
            << t.idealProcessorKnown << L' ' << t.idealProcessor.Group << L' '
            << static_cast<unsigned>(t.idealProcessor.Number) << L' ' << t.mmCriticalKnown << L' '
            << t.mmCritical << L' ' << t.pagePriorityKnown << L' ' << t.pagePriority << L' '
            << t.ioPriorityKnown << L' ' << t.ioPriority << L' ' << t.powerThrottleKnown << L' '
            << t.powerThrottle.controlMask << L' ' << t.powerThrottle.stateMask << L' '
            << t.suspensionAdded << L"\n";
    }
    for (const auto& pair : m_services) out << L"S " << pair.second << L' ' << pair.first << L"\n";
    for (const auto& p : m_powerSettings) {
        wchar_t scheme[64]{}, subgroup[64]{}, setting[64]{};
        StringFromGUID2(p.scheme, scheme, 64);
        StringFromGUID2(p.subgroup, subgroup, 64);
        StringFromGUID2(p.setting, setting, 64);
        out << L"Q " << scheme << L' ' << subgroup << L' ' << setting << L' '
            << p.acValue << L"\n";
    }
    for (const auto& r : m_registry) {
        if (r.kind == 4) {
            out << L"G " << r.rootId << L' ' << (r.existed ? 1 : 0) << L' '
                << r.keyPath.size() << L'|' << r.keyPath << L'|'
                << r.valueName.size() << L'|' << r.valueName << L'|' << r.raw << L"\n";
        } else {
            out << L"R " << r.kind << L' ' << (r.existed ? 1 : 0) << L' '
                << r.valueName.size() << L'|' << r.valueName << L'|' << r.raw << L"\n";
        }
    }
    out.flush();
    out.close();
    if (!out) {
        Log(L"snapshot", L"cannot flush journal %s", temp.c_str());
        return false;
    }
    if (!MoveFileExW(temp.c_str(), m_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Log(L"snapshot", L"cannot commit journal %s (error %lu)", m_path.c_str(), GetLastError());
        return false;
    }
    return true;
}

bool SessionSnapshot::LoadJournal() {
    ClearMemory();
    std::wifstream in(m_path);
    if (!in.is_open()) return false;
    std::wstring line;
    if (!std::getline(in, line) ||
        (line != L"KT_SESSION_JOURNAL_V1" && line != L"KT_SESSION_JOURNAL_V2" &&
         line != L"KT_SESSION_JOURNAL_V3")) return false;
    const bool hasGpuPriority = line == L"KT_SESSION_JOURNAL_V3";
    while (std::getline(in, line)) {
        std::wstringstream fields(line);
        wchar_t type = 0;
        fields >> type;
        if (type == L'W') {
            std::wstring guid;
            fields >> guid;
            if (SUCCEEDED(CLSIDFromString(const_cast<wchar_t*>(guid.c_str()), &m_powerScheme))) m_powerKnown = true;
        } else if (type == L'P') {
            ProcessState p;
            int priority = 0, ioKnown = 0, memKnown = 0, ecoKnown = 0, eco = 0, cpuKnown = 0;
            ULONGLONG affinity = 0;
            std::wstring cpuText;
            if (fields >> p.pid >> p.creationTime >> priority >> affinity >> ioKnown >> p.ioPriority
                >> memKnown >> p.memoryPriority >> ecoKnown >> eco >> cpuKnown >> cpuText) {
                int gpuKnown = 0;
                if (hasGpuPriority) fields >> gpuKnown >> p.gpuPriority;
                p.priority = static_cast<PriorityClass>(priority);
                p.affinity = static_cast<DWORD_PTR>(affinity);
                p.ioKnown = ioKnown != 0; p.memoryKnown = memKnown != 0;
                p.ecoKnown = ecoKnown != 0; p.ecoQos = eco != 0;
                p.cpuSetsKnown = cpuKnown != 0; p.cpuSets = CpuSetsFromText(cpuText);
                p.gpuPriorityKnown = gpuKnown != 0;
                m_processes[p.pid] = p;
            }
        } else if (type == L'T') {
            ThreadInfo t;
            unsigned idealNumber = 0;
            int priorityKnown = 0, boostKnown = 0, boostDisabled = 0, idealKnown = 0;
            int mmKnown = 0, mmCritical = 0, pageKnown = 0, ioKnown = 0, powerKnown = 0, suspended = 0;
            if (fields >> t.tid >> t.pid >> t.creationTime >> t.processCreationTime >> t.basePriority
                >> priorityKnown >> boostKnown >> boostDisabled >> idealKnown >> t.idealProcessor.Group
                >> idealNumber >> mmKnown >> mmCritical >> pageKnown >> t.pagePriority
                >> ioKnown >> t.ioPriority >> powerKnown >> t.powerThrottle.controlMask
                >> t.powerThrottle.stateMask >> suspended) {
                t.priorityKnown = priorityKnown != 0;
                t.priorityBoostKnown = boostKnown != 0;
                t.priorityBoostDisabled = boostDisabled != 0;
                t.idealProcessorKnown = idealKnown != 0;
                t.idealProcessor.Number = static_cast<BYTE>(idealNumber);
                t.mmCriticalKnown = mmKnown != 0;
                t.mmCritical = mmCritical != 0;
                t.pagePriorityKnown = pageKnown != 0;
                t.ioPriorityKnown = ioKnown != 0;
                t.powerThrottleKnown = powerKnown != 0;
                t.suspensionAdded = suspended != 0;
                m_threads[{t.tid, t.creationTime}] = t;
            } else {
                m_legacyThreadRecords = true;
            }
        } else if (type == L'S') {
            int action = 0; std::wstring name;
            if (fields >> action >> name) m_services[name] = action;
        } else if (type == L'Q') {
            std::wstring schemeText, subgroupText, settingText;
            PowerState p;
            if (fields >> schemeText >> subgroupText >> settingText >> p.acValue &&
                SUCCEEDED(CLSIDFromString(const_cast<wchar_t*>(schemeText.c_str()), &p.scheme)) &&
                SUCCEEDED(CLSIDFromString(const_cast<wchar_t*>(subgroupText.c_str()), &p.subgroup)) &&
                SUCCEEDED(CLSIDFromString(const_cast<wchar_t*>(settingText.c_str()), &p.setting)))
                m_powerSettings.push_back(p);
        } else if (type == L'G') {
            int rootId = 0, existed = 0;
            std::wstring payload;
            if (fields >> rootId >> existed) {
                std::getline(fields, payload);
                const size_t start = payload.find_first_not_of(L" \t");
                if (start != std::wstring::npos) payload.erase(0, start);
                const size_t bar1 = payload.find(L'|');
                if (bar1 != std::wstring::npos) {
                    const size_t pathLen = static_cast<size_t>(wcstoul(payload.substr(0, bar1).c_str(), nullptr, 10));
                    const size_t pathStart = bar1 + 1;
                    const size_t bar2 = payload.find(L'|', pathStart + pathLen);
                    if (bar2 != std::wstring::npos && pathLen <= payload.size() - pathStart) {
                        const size_t pathEnd = pathStart + pathLen;
                        const size_t bar3 = payload.find(L'|', bar2 + 1);
                        if (bar3 != std::wstring::npos) {
                            const size_t nameLen = static_cast<size_t>(wcstoul(
                                payload.substr(bar2 + 1, bar3 - bar2 - 1).c_str(), nullptr, 10));
                            const size_t nameStart = bar3 + 1;
                            if (nameLen <= payload.size() - nameStart) {
                                const size_t nameEnd = nameStart + nameLen;
                                if (nameEnd < payload.size() && payload[nameEnd] == L'|') {
                                    RegistryState r;
                                    r.kind = 4;
                                    r.rootId = rootId;
                                    r.existed = existed != 0;
                                    r.keyPath = payload.substr(pathStart, pathLen);
                                    r.valueName = payload.substr(nameStart, nameLen);
                                    r.raw = payload.substr(nameEnd + 1);
                                    if (r.rootId == 1 || r.rootId == 2) m_registry.push_back(std::move(r));
                                }
                            }
                        }
                    }
                }
            }
        } else if (type == L'R') {
            int kind = 0, existed = 0;
            std::wstring payload;
            if (fields >> kind >> existed) {
                std::getline(fields, payload);
                const size_t start = payload.find_first_not_of(L" \t");
                if (start != std::wstring::npos) payload.erase(0, start);
                const size_t sep1 = payload.find(L'|');
                if (sep1 != std::wstring::npos) {
                    RegistryState r;
                    r.kind = kind;
                    r.existed = existed != 0;
                    const size_t nameLen = (size_t)wcstoul(payload.substr(0, sep1).c_str(), nullptr, 10);
                    std::wstring rest = payload.substr(sep1 + 1);
                    if (nameLen <= rest.size()) {
                        r.valueName = rest.substr(0, nameLen);
                        r.raw = rest.size() > nameLen + 1 ? rest.substr(nameLen + 1) : L"";
                        m_registry.push_back(r);
                    }
                }
            }
        } else if (type == L'E') {
            m_extremeActive = true;
        } else if (type == L'X') {
            m_legacyThreadRecords = true;
        }
    }
    return HasPendingChanges();
}

bool SessionSnapshot::RestoreProcess(const ProcessState& state) const {
    HANDLE process = OpenProcessForOpt(state.pid);
    if (!process) return !IsProcessRunning(state.pid);
    if (!ProcessStillMatches(process, state.creationTime)) {
        CloseHandle(process);
        Log(L"snapshot", L"PID %lu was reused; skipped journal restore", state.pid);
        return true;
    }
    bool ok = true;
    if (state.priority != PriorityClass::None) ok = SetProcPriorityClass(process, state.priority) && ok;
    if (state.affinity) ok = SetProcAffinity(process, state.affinity) && ok;
    if (state.ioKnown) ok = SetProcIoPriority(process, state.ioPriority) && ok;
    if (state.memoryKnown) ok = SetProcMemoryPriority(process, state.memoryPriority) && ok;
    if (state.ecoKnown) ok = SetProcEcoQoS(process, state.ecoQos) && ok;
    if (state.cpuSetsKnown) ok = SetProcessCpuSets(process, state.cpuSets) && ok;
    if (state.gpuPriorityKnown)
        ok = SetGpuSchedulingPriority(process, state.gpuPriority) && ok;
    CloseHandle(process);
    return ok;
}

bool SessionSnapshot::RestoreProcessForPid(DWORD pid) {
    auto found = m_processes.find(pid);
    if (found == m_processes.end()) return true;
    const ProcessState original = found->second;
    if (!RestoreProcess(original)) return false;
    m_processes.erase(found);
    if (!HasPendingChanges()) {
        if (DeleteFileW(m_path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) return true;
    } else if (Persist()) {
        return true;
    }
    m_processes[pid] = original;
    Log(L"snapshot", L"could not commit process PID %lu restoration", pid);
    return false;
}

bool SessionSnapshot::RestoreThreadRecord(ThreadInfo& state) {
    HANDLE thread = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION |
                               THREAD_SUSPEND_RESUME, FALSE, state.tid);
    if (!thread) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (Thread32First(snap, &entry)) {
            do {
                if (entry.th32ThreadID == state.tid && entry.th32OwnerProcessID == state.pid) {
                    found = true;
                    break;
                }
            } while (Thread32Next(snap, &entry));
        }
        CloseHandle(snap);
        return !found;
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
        CloseHandle(thread);
        return false;
    }
    const ULONGLONG threadCreated = FileTimeValue(created);
    if (threadCreated != state.creationTime || GetProcessIdOfThread(thread) != state.pid) {
        CloseHandle(thread);
        return true;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, state.pid);
    if (!process) {
        CloseHandle(thread);
        return false;
    }
    FILETIME processCreated{};
    const bool processTimeKnown = GetProcessTimes(process, &processCreated, &exited, &kernel, &user) != FALSE;
    CloseHandle(process);
    if (!processTimeKnown) {
        CloseHandle(thread);
        return false;
    }
    if (FileTimeValue(processCreated) != state.processCreationTime) {
        CloseHandle(thread);
        return true;
    }

    bool ok = RestoreThreadInfo(thread, state);
    if (ok && state.suspensionAdded) {
        LONG previous = 0;
        ok = ResumeThreadSafe(thread, &previous);
        if (ok) {
            state.suspensionAdded = false;
            if (!Persist()) {
                Log(L"snapshot", L"could not persist resumed state for TID %lu", state.tid);
                ok = false;
            }
        }
    }
    CloseHandle(thread);
    return ok;
}

bool SessionSnapshot::RestoreProcessThreads(DWORD pid) {
    std::vector<ThreadKey> targets;
    for (const auto& pair : m_threads)
        if (pair.second.pid == pid) targets.push_back(pair.first);
    if (targets.empty()) return true;
    bool ok = true;
    for (const auto& key : targets) {
        auto found = m_threads.find(key);
        if (found == m_threads.end()) continue;
        if (!RestoreThreadRecord(found->second)) {
            ok = false;
            continue;
        }
        const ThreadInfo restored = found->second;
        m_threads.erase(found);
        const bool saved = HasPendingChanges()
            ? Persist()
            : (DeleteFileW(m_path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND);
        if (!saved) {
            m_threads[key] = restored;
            Log(L"snapshot", L"could not commit thread restoration for TID %lu", key.tid);
            ok = false;
        }
    }
    if (!ok) Log(L"snapshot", L"thread restoration incomplete for PID %lu", pid);
    return ok;
}

bool SessionSnapshot::RestoreRegistryRecord(const RegistryState& state) const {
    bool ok = false;
    if (state.kind == 4) {
        HKEY root = state.rootId == 1 ? HKEY_CURRENT_USER
                  : state.rootId == 2 ? HKEY_LOCAL_MACHINE : nullptr;
        if (!root) return false;
        HKEY h = nullptr;
        LONG openResult = state.existed
            ? RegCreateKeyExW(root, state.keyPath.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                              nullptr, &h, nullptr)
            : RegOpenKeyExW(root, state.keyPath.c_str(), 0, KEY_SET_VALUE, &h);
        if (openResult == ERROR_SUCCESS) {
            if (state.existed) {
                DWORD value = static_cast<DWORD>(wcstoul(state.raw.c_str(), nullptr, 10));
                ok = RegSetValueExW(h, state.valueName.c_str(), 0, REG_DWORD,
                                    reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
            } else {
                const LSTATUS status = RegDeleteValueW(h, state.valueName.c_str());
                ok = status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
            }
            RegCloseKey(h);
        } else if (!state.existed && openResult == ERROR_FILE_NOT_FOUND) {
            ok = true;
        }
    } else if (state.kind == 1 || state.kind == 2) {
        const wchar_t* keyPath = state.kind == 1 ? L"System\\GameConfigStore"
                                                 : L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR";
        const wchar_t* valueName = state.kind == 1 ? L"GameDVR_Enabled" : L"AppCaptureEnabled";
        HKEY h = nullptr;
        LONG openResult = state.existed
            ? RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_SET_VALUE,
                              nullptr, &h, nullptr)
            : RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_SET_VALUE, &h);
        if (openResult == ERROR_SUCCESS) {
            if (state.existed) {
                DWORD v = (DWORD)wcstoul(state.raw.c_str(), nullptr, 10);
                ok = RegSetValueExW(h, valueName, 0, REG_DWORD, (const BYTE*)&v, sizeof(v)) == ERROR_SUCCESS;
            } else {
                LSTATUS st = RegDeleteValueW(h, valueName);
                ok = st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
            }
            RegCloseKey(h);
        } else if (!state.existed && openResult == ERROR_FILE_NOT_FOUND) ok = true;
    } else if (state.kind == 3) {
        if (state.valueName.empty()) return true;
        HKEY h = nullptr;
        const wchar_t* keyPath = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
        LONG openResult = state.existed
            ? RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_SET_VALUE,
                              nullptr, &h, nullptr)
            : RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_SET_VALUE, &h);
        if (openResult == ERROR_SUCCESS) {
            if (state.existed) {
                ok = RegSetValueExW(h, state.valueName.c_str(), 0, REG_SZ,
                                    (const BYTE*)state.raw.c_str(),
                                    (DWORD)((state.raw.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
            } else {
                LSTATUS st = RegDeleteValueW(h, state.valueName.c_str());
                ok = st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
            }
            RegCloseKey(h);
        } else if (!state.existed && openResult == ERROR_FILE_NOT_FOUND) ok = true;
    }
    if (!ok) Log(L"snapshot", L"registry restore failed for kind %d", state.kind);
    return ok;
}

bool SessionSnapshot::RestorePowerSetting(const PowerState& state) const {
    const DWORD status = PowerWriteACValueIndex(nullptr, &state.scheme, &state.subgroup,
                                                &state.setting, state.acValue);
    if (status != ERROR_SUCCESS)
        Log(L"snapshot", L"power setting restore failed (error %lu)", status);
    return status == ERROR_SUCCESS;
}

bool SessionSnapshot::RestoreAll() {
    if (!HasPendingChanges()) return true;
    bool ok = true;
    if (m_extremeActive) {
        if (!RecoverExtremeShell())
            ok = false;
        else
            m_extremeActive = false;
    }
    if (m_legacyThreadRecords) {
        Log(L"error", L"legacy thread journal lacks safe identity data; manual recovery required");
        ok = false;
    }
    std::vector<ThreadKey> threadKeys;
    for (const auto& pair : m_threads) threadKeys.push_back(pair.first);
    for (const auto& key : threadKeys) {
        auto found = m_threads.find(key);
        if (found == m_threads.end()) continue;
        if (!RestoreThreadRecord(found->second)) {
            ok = false;
            continue;
        }
        const ThreadInfo restored = found->second;
        m_threads.erase(found);
        const bool saved = HasPendingChanges()
            ? Persist()
            : (DeleteFileW(m_path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND);
        if (!saved) {
            m_threads[key] = restored;
            Log(L"snapshot", L"could not commit thread restoration for TID %lu", key.tid);
            ok = false;
        }
    }
    for (const auto& pair : m_processes) ok = RestoreProcess(pair.second) && ok;
    for (const auto& r : m_registry) ok = RestoreRegistryRecord(r) && ok;
    for (const auto& pair : m_services) {
        bool restored = pair.second == 1 ? ServiceResume(pair.first) : ServiceStart(pair.first);
        if (!restored) ok = false;
    }
    for (const auto& setting : m_powerSettings) ok = RestorePowerSetting(setting) && ok;
    if (m_powerKnown && PowerSetActiveScheme(nullptr, &m_powerScheme) != ERROR_SUCCESS) ok = false;
    if (ok) {
        if (!DeleteFileW(m_path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
            Persist();
            Log(L"error", L"restored state but could not remove session journal");
            return false;
        }
        ClearMemory();
        Log(L"snapshot", L"all recorded changes restored");
    } else {
        Persist();
        Log(L"error", L"snapshot restore incomplete; journal kept for recovery");
    }
    return ok;
}

bool SessionSnapshot::RecoverPendingJournal() {
    if (GetFileAttributesW(m_path.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    if (!LoadJournal()) {
        Log(L"error", L"invalid session journal retained at %s", m_path.c_str());
        return false;
    }
    Log(L"snapshot", L"pending session journal found; attempting recovery");
    return RestoreAll();
}

}
