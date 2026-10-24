#include "platform.h"
#include "util.h"

namespace kt {
namespace {
typedef LONG (WINAPI* RtlGetVersion_t)(OSVERSIONINFOW*);

bool ReadDword(const wchar_t* path, const wchar_t* name) {
    DWORD value = 0, size = sizeof(value), type = 0;
    return RegGetValueW(HKEY_LOCAL_MACHINE, path, name, RRF_RT_REG_DWORD,
                        &type, &value, &size) == ERROR_SUCCESS && value != 0;
}

bool IsElevatedToken() {
    HANDLE token = nullptr;
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    bool ok = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
              GetTokenInformation(token, TokenElevation, &elevation, size, &size);
    if (token) CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}
}

PlatformCapabilities DetectPlatformCapabilities() {
    PlatformCapabilities caps;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    RtlGetVersion_t getVersion = reinterpret_cast<RtlGetVersion_t>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (getVersion && getVersion(&version) == 0) {
        caps.major = version.dwMajorVersion;
        caps.minor = version.dwMinorVersion;
        caps.build = version.dwBuildNumber;
    }
    caps.elevated = IsElevatedToken();
    caps.hypervisor = IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED) != FALSE;
    caps.vbs = ReadDword(L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard", L"EnableVirtualizationBasedSecurity");
    caps.hvci = ReadDword(L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity", L"Enabled");
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    caps.cpuSets = GetProcAddress(kernel, "GetSystemCpuSetInformation") != nullptr &&
                   GetProcAddress(kernel, "SetProcessDefaultCpuSets") != nullptr;
    caps.ecoQos = GetProcAddress(kernel, "SetProcessInformation") != nullptr;
    caps.etw = GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "StartTraceW") != nullptr;
    caps.hybridCertain = caps.cpuSets;
    return caps;
}

void LogPlatformCapabilities(const PlatformCapabilities& caps) {
    Log(L"platform", L"Windows %lu.%lu build %lu · admin=%s · Hyper-V=%s · VBS=%s · HVCI=%s",
        caps.major, caps.minor, caps.build, caps.elevated ? L"yes" : L"no",
        caps.hypervisor ? L"on" : L"off", caps.vbs ? L"on" : L"off", caps.hvci ? L"on" : L"off");
    Log(L"platform", L"CPU Sets=%s · EcoQoS=%s · ETW=%s · hybrid topology=%s",
        caps.cpuSets ? L"available" : L"fallback: no pinning",
        caps.ecoQos ? L"available" : L"fallback: priority only",
        caps.etw ? L"available" : L"fallback: polling",
        caps.hybridCertain ? L"verified through CPU Sets" : L"uncertain; affinity disabled");
}

}
