#include "gpumon.h"
#include <pdh.h>
#include <pdhmsg.h>

#include <cstring>
#include <cwchar>
#include <map>
#include <mutex>
#include <vector>

#pragma comment(lib, "pdh.lib")

namespace kt {

namespace {

std::mutex              g_mtx;
PDH_HQUERY              g_query       = nullptr;
PDH_HCOUNTER            g_counter     = nullptr;
std::map<DWORD, double> g_procPct;
bool                    g_haveSample  = false;
int                     g_sampleCount = 0;
ULONGLONG               g_retryAt     = 0;
wchar_t                 g_adapter[64] = L"";

constexpr int       kReopenSamples = 30;
constexpr ULONGLONG kRetryMs       = 30000;

void CloseQueryLocked() {
    if (g_query) { PdhCloseQuery(g_query); g_query = nullptr; }
    g_counter    = nullptr;
    g_haveSample = false;
    g_sampleCount = 0;
}

bool OpenQueryLocked() {
    if (g_query) return true;
    const ULONGLONG now = GetTickCount64();
    if (now < g_retryAt) return false;

    if (PdhOpenQuery(nullptr, 0, &g_query) != ERROR_SUCCESS) {
        g_query   = nullptr;
        g_retryAt = now + kRetryMs;
        return false;
    }
    if (PdhAddEnglishCounterW(g_query, L"\\GPU Engine(*)\\Utilization Percentage",
                              0, &g_counter) != ERROR_SUCCESS) {
        CloseQueryLocked();
        g_retryAt = now + kRetryMs;
        return false;
    }
    g_retryAt = 0;
    return true;
}

DWORD PidFromInstanceName(const wchar_t* name) {
    if (!name || std::wcsncmp(name, L"pid_", 4) != 0) return 0;
    return (DWORD)std::wcstol(name + 4, nullptr, 10);
}

void CollectLocked() {
    if (!OpenQueryLocked()) return;

    if (PdhCollectQueryData(g_query) != ERROR_SUCCESS) {
        CloseQueryLocked();
        g_retryAt = GetTickCount64() + kRetryMs;
        return;
    }
    g_sampleCount++;

    if (!g_haveSample) {
        g_haveSample = true;
        return;
    }

    DWORD size = 0, count = 0;
    PDH_STATUS st = PdhGetFormattedCounterArrayW(g_counter, PDH_FMT_DOUBLE,
                                                 &size, &count, nullptr);
    if (st == ERROR_SUCCESS) {
        g_procPct.clear();
        return;
    }
    if (st != PDH_MORE_DATA || size == 0) return;

    std::vector<BYTE> buf(size);
    st = PdhGetFormattedCounterArrayW(g_counter, PDH_FMT_DOUBLE,
                                      &size, &count,
                                      (PPDH_FMT_COUNTERVALUE_ITEM_W)buf.data());
    if (st != ERROR_SUCCESS) return;

    std::map<DWORD, double> agg;
    auto* items = (PPDH_FMT_COUNTERVALUE_ITEM_W)buf.data();
    for (DWORD i = 0; i < count; i++) {
        const DWORD pid = PidFromInstanceName(items[i].szName);
        if (!pid) continue;
        if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
            items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
        double v = items[i].FmtValue.doubleValue;
        if (v < 0.0) v = 0.0;
        agg[pid] += v;
    }
    g_procPct = std::move(agg);

    if (g_sampleCount >= kReopenSamples) CloseQueryLocked();
}

void RefreshAdapterLocked() {
    DISPLAY_DEVICEW dd{};
    wchar_t fallback[64] = L"";
    for (DWORD i = 0; ; i++) {
        dd.cb = sizeof(dd);
        if (!EnumDisplayDevicesW(nullptr, i, &dd, 0)) break;
        if (dd.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) continue;
        if (!dd.DeviceString[0]) continue;
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
            wcsncpy_s(g_adapter, 64, dd.DeviceString, _TRUNCATE);
            return;
        }
        if (!fallback[0]) wcsncpy_s(fallback, 64, dd.DeviceString, _TRUNCATE);
    }
    if (fallback[0] && !g_adapter[0]) wcsncpy_s(g_adapter, 64, fallback, _TRUNCATE);
}

}

bool GpuMon_Init() {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_procPct.clear();
    g_adapter[0] = L'\0';
    g_retryAt    = 0;
    RefreshAdapterLocked();
    return OpenQueryLocked();
}

void GpuMon_Shutdown() {
    std::lock_guard<std::mutex> lk(g_mtx);
    CloseQueryLocked();
    g_procPct.clear();
    g_adapter[0] = L'\0';
    g_retryAt    = 0;
}

double GpuMon_TotalPct() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!OpenQueryLocked()) return -1.0;
    CollectLocked();
    double total = 0.0;
    for (const auto& kv : g_procPct) total += kv.second;
    if (total > 100.0) total = 100.0;
    return total;
}

bool GpuMon_ProcessPct(DWORD pid, double* outPct) {
    if (outPct) *outPct = -1.0;
    if (!pid || !outPct) return false;
    std::lock_guard<std::mutex> lk(g_mtx);
    const auto it = g_procPct.find(pid);
    if (it == g_procPct.end()) return false;
    double v = it->second;
    if (v < 0.0)   v = 0.0;
    if (v > 100.0) v = 100.0;
    *outPct = v;
    return true;
}

void GpuMon_AdapterName(wchar_t* out, int cch) {
    if (!out || cch <= 0) return;
    out[0] = L'\0';
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_adapter[0]) RefreshAdapterLocked();
    if (g_adapter[0]) wcsncpy_s(out, cch, g_adapter, _TRUNCATE);
}

}
