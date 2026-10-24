#pragma once
#ifndef KT_GPUMON_H
#define KT_GPUMON_H

#include <windows.h>

namespace kt {

bool GpuMon_Init();
void GpuMon_Shutdown();
double GpuMon_TotalPct();
bool GpuMon_ProcessPct(DWORD pid, double* outPct);
void GpuMon_AdapterName(wchar_t* out, int cch);

}

#endif
