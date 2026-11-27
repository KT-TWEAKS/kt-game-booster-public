#include "metrics.h"
#include <algorithm>
#include <vector>

namespace kt {

void MetricsHistory::Push(const MetricsSample& s) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_buf.push_back(s);
    if (m_buf.size() > kCap) m_buf.erase(m_buf.begin());
}

void MetricsHistory::Snapshot(std::vector<MetricsSample>& out, size_t want) {
    std::lock_guard<std::mutex> lk(m_mtx);
    out.clear();
    if (m_buf.empty()) return;
    size_t n = (std::min)(want, m_buf.size());
    out.reserve(n);
    for (size_t i = m_buf.size() - n; i < m_buf.size(); ++i) {
        out.push_back(m_buf[i]);
    }
}

void MetricsHistory::Clear() {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_buf.clear();
}

static BenchResult g_last;

BenchResult RunBench(DWORD durationMs) {
    BenchResult r;
    const DWORD kMin = 200;
    const DWORD kMax = 5000;
    if (durationMs < kMin) durationMs = kMin;
    if (durationMs > kMax) durationMs = kMax;

    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return r;

    std::vector<uint32_t> deltas;
    deltas.reserve(durationMs * 100);

    ULONGLONG t0 = GetTickCount64();
    while ((GetTickCount64() - t0) < durationMs) {
        LARGE_INTEGER before{}, after{};
        QueryPerformanceCounter(&before);
        if (!SwitchToThread()) Sleep(0);
        QueryPerformanceCounter(&after);
        LONGLONG ticks = after.QuadPart - before.QuadPart;
        if (ticks < 0) continue;
        uint64_t us = ticks > 0
            ? (uint64_t)((ticks * 1000000LL + frequency.QuadPart - 1) / frequency.QuadPart)
            : 0;
        if (us <= 1000000ULL) deltas.push_back((uint32_t)us);
    }
    ULONGLONG t1 = GetTickCount64();
    r.durationMs = (uint32_t)(t1 - t0);
    r.tickMs = t1;
    r.samples = (uint32_t)deltas.size();

    if (!deltas.empty()) {
        uint64_t sum = 0;
        uint32_t mx = 0;
        for (uint32_t d : deltas) {
            sum += d;
            if (d > mx) mx = d;
        }
        r.latencyUsAvg = (uint32_t)(sum / deltas.size());
        r.latencyUsMax = mx;
        std::sort(deltas.begin(), deltas.end());
        r.latencyUsP50 = deltas[deltas.size() / 2];
        size_t p95 = (size_t)((deltas.size() - 1) * 0.95);
        r.latencyUsP95 = deltas[p95];
        r.valid = true;
    }

    g_last = r;
    return r;
}

const BenchResult& LatestBench() { return g_last; }
void ClearBench() { g_last = BenchResult{}; }

}
