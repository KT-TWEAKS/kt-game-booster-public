#pragma once
#ifndef KT_METRICS_H
#define KT_METRICS_H

#include <windows.h>
#include <vector>
#include <mutex>
#include <cstdint>

namespace kt {

struct MetricsSample {
    double      cpuPct;
    uint64_t    ramUsedMB;
    uint64_t    ramTotalMB;
    uint32_t    fps;
    uint32_t    pingMs;
    ULONGLONG   tickMs;
    double      gpuPct;
};

class MetricsHistory {
public:
    static constexpr size_t kCap = 600;

    void Push(const MetricsSample& s);
    void Snapshot(std::vector<MetricsSample>& out, size_t want = kCap);
    void Clear();

private:
    std::mutex              m_mtx;
    std::vector<MetricsSample> m_buf;
};

struct BenchResult {
    uint32_t    latencyUsAvg = 0;
    uint32_t    latencyUsP50 = 0;
    uint32_t    latencyUsP95 = 0;
    uint32_t    latencyUsMax = 0;
    uint32_t    samples = 0;
    uint32_t    durationMs = 0;
    ULONGLONG   tickMs = 0;
    bool        valid = false;
};

BenchResult RunBench(DWORD durationMs = 1500);
const BenchResult& LatestBench();
void ClearBench();

}

#endif
