#include "winsvcmon.h"
#include "util.h"
#include <winsvc.h>

namespace kt {

static const wchar_t* kGamingTargets[] = {
    L"WSearch", L"SysMain", L"wuauserv", L"Spooler",
    L"WpcMonSvc", L"MapsBroker", L"lfsvc", L"Fax",
    L"DiagTrack", L"WerSvc", L"DoSvc", L"RemoteRegistry",
    L"TrkWks", L"RetailDemo", L"WMPNetworkSvc", L"PhoneSvc",
    L"WalletService", L"shpamsvc", L"DusmSvc", L"PrintNotify",
    nullptr
};

static const wchar_t* kGamingTargetBases[] = {
    L"BcastDVRUserService", L"CaptureService", L"OneSyncSvc",
    L"PimIndexMaintenanceSvc", L"UnistoreSvc", L"UserDataSvc",
    L"CDPUserSvc", nullptr
};

static const wchar_t* kProtectedServices[] = {
    L"RpcSs", L"DcomLaunch", L"RpcEptMapper", L"LSM", L"PlugPlay",
    L"Power", L"EventLog", L"SamSs", L"WinDefend", L"mpssvc", L"BFE",
    L"Dhcp", L"Dnscache", L"nsi", L"LanmanWorkstation", L"Schedule",
    L"Winmgmt", L"ProfSvc", L"UserManager", L"gpsvc", L"CryptSvc",
    L"Appinfo", L"AudioEndpointBuilder", L"Audiosrv", L"WlanSvc",
    L"NlaSvc", L"netprofm", L"W32Time", L"hidserv",
    L"DeviceAssociationService", L"InstallService", L"AppXSvc",
    L"ClipSVC", L"StateRepository", L"BITS", L"GamingServices",
    L"GamingServicesNet", L"XblAuthManager", L"XblGameSave",
    L"XboxGipSvc", L"XboxNetApiSvc", L"GameInputSvc", nullptr
};

static bool NameInList(const std::wstring& name, const wchar_t* const* list) {
    for (int i = 0; list[i]; ++i)
        if (_wcsicmp(name.c_str(), list[i]) == 0) return true;
    return false;
}

static bool NameMatchesPerUserBase(const std::wstring& name, const wchar_t* const* bases) {
    for (int i = 0; bases[i]; ++i) {
        const size_t length = wcslen(bases[i]);
        if (name.size() > length && name[length] == L'_' &&
            _wcsnicmp(name.c_str(), bases[i], length) == 0)
            return true;
    }
    return false;
}

bool IsGamingServiceTarget(const std::wstring& name) {
    return NameInList(name, kGamingTargets) ||
           NameMatchesPerUserBase(name, kGamingTargetBases);
}

bool IsProtectedService(const std::wstring& name) {
    return NameInList(name, kProtectedServices);
}

static bool ScmControl(const std::wstring& name, DWORD control) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    bool ok = false;
    const DWORD access = SERVICE_QUERY_STATUS |
        (control == SERVICE_CONTROL_STOP ? SERVICE_STOP : SERVICE_PAUSE_CONTINUE);
    SC_HANDLE svc = OpenServiceW(scm, name.c_str(), access);
    if (svc) {
        SERVICE_STATUS st;
        ok = ControlService(svc, control, &st) != FALSE;
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ok;
}

static DWORD QueryServiceState(SC_HANDLE svc) {
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes = 0;
    if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<BYTE*>(&status), sizeof(status), &bytes))
        return 0;
    return status.dwCurrentState;
}

static bool WaitForServiceState(SC_HANDLE svc, DWORD wanted, DWORD timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    do {
        if (QueryServiceState(svc) == wanted) return true;
        Sleep(100);
    } while (GetTickCount64() < deadline);
    return QueryServiceState(svc) == wanted;
}

int EnumServices(std::vector<ServiceInfo>& out) {
    out.clear();
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return 0;

    DWORD bytes = 0, count = 0, handle = 0, resume = 0;
    DWORD needed = 96 * 1024;
    std::vector<BYTE> buf;
    for (;;) {
        buf.assign(needed, 0);
        BOOL ok = EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                                        buf.data(), (DWORD)buf.size(),
                                        &bytes, &count, &resume, nullptr);
        if (!ok && GetLastError() == ERROR_MORE_DATA) {
            needed = bytes + 16 * 1024;
            continue;
        }
        if (!ok) { CloseServiceHandle(scm); return 0; }
        break;
    }

    out.reserve(count);
    auto* entries = (ENUM_SERVICE_STATUS_PROCESSW*)buf.data();
    for (DWORD i = 0; i < count && out.size() < 512; i++) {
        const ENUM_SERVICE_STATUS_PROCESSW& e = entries[i];
        ServiceInfo s;
        wcsncpy_s(s.name, 64, e.lpServiceName, _TRUNCATE);
        wcsncpy_s(s.displayName, 96, e.lpDisplayName ? e.lpDisplayName : L"", _TRUNCATE);
        s.state = e.ServiceStatusProcess.dwCurrentState;
        s.pid = e.ServiceStatusProcess.dwProcessId;
        s.canStop = (e.ServiceStatusProcess.dwControlsAccepted & SERVICE_ACCEPT_STOP) ? 1 : 0;
        s.canPause = (e.ServiceStatusProcess.dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE) ? 1 : 0;
        s.gamingTarget = IsGamingServiceTarget(s.name) ? 1 : 0;
        s.protectedService = IsProtectedService(s.name) ? 1 : 0;
        out.push_back(std::move(s));
    }
    CloseServiceHandle(scm);
    return (int)out.size();
}

bool ServiceStart(const std::wstring& name) {
    if (IsProtectedService(name) || !IsGamingServiceTarget(name)) return false;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    bool ok = false;
    SC_HANDLE svc = OpenServiceW(scm, name.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
    if (svc) {
        DWORD state = QueryServiceState(svc);
        if (state == SERVICE_RUNNING) {
            ok = true;
        } else {
            if (state == SERVICE_STOP_PENDING)
                WaitForServiceState(svc, SERVICE_STOPPED, 10000);
            state = QueryServiceState(svc);
            if (state == SERVICE_START_PENDING) {
                ok = WaitForServiceState(svc, SERVICE_RUNNING, 10000);
            } else if (state == SERVICE_STOPPED) {
                ok = StartServiceW(svc, 0, nullptr) != FALSE ||
                     GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
                if (ok) ok = WaitForServiceState(svc, SERVICE_RUNNING, 10000);
            }
        }
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ok;
}

bool ServiceStop(const std::wstring& name)  { return IsGamingServiceTarget(name) && !IsProtectedService(name) && ScmControl(name, SERVICE_CONTROL_STOP); }
bool ServicePause(const std::wstring& name){ return IsGamingServiceTarget(name) && !IsProtectedService(name) && ScmControl(name, SERVICE_CONTROL_PAUSE); }
bool ServiceResume(const std::wstring& name) {
    if (!IsGamingServiceTarget(name) || IsProtectedService(name)) return false;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    bool ok = false;
    SC_HANDLE svc = OpenServiceW(scm, name.c_str(), SERVICE_PAUSE_CONTINUE | SERVICE_QUERY_STATUS);
    if (svc) {
        if (QueryServiceState(svc) == SERVICE_RUNNING) ok = true;
        else {
            SERVICE_STATUS status{};
            ok = ControlService(svc, SERVICE_CONTROL_CONTINUE, &status) != FALSE &&
                 WaitForServiceState(svc, SERVICE_RUNNING, 10000);
        }
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ok;
}

}
