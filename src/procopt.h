#pragma once
#ifndef KT_PROCOPT_H
#define KT_PROCOPT_H

#include <windows.h>
#include <string>
#include "config.h"

namespace kt {

class SessionSnapshot;

HANDLE OpenProcessForOpt(DWORD pid);

bool SetProcPriorityClass(HANDLE h, PriorityClass pc);
bool GetProcPriorityClass(HANDLE h, PriorityClass* out);
DWORD   PriorityClassToWin(PriorityClass pc);

bool SuspendProcess(HANDLE h);
bool ResumeProcess(HANDLE h);

bool SetProcAffinity(HANDLE h, DWORD_PTR mask);
bool GetProcAffinity(HANDLE h, DWORD_PTR* sys, DWORD_PTR* proc);

bool TrimWorkingSet(HANDLE h);

bool SetProcEcoQoS(HANDLE h, bool on);
bool GetProcEcoQoS(HANDLE h, bool* out);

bool SetProcIoPriority(HANDLE h, int priority);
bool GetProcIoPriority(HANDLE h, int* out);

bool SetGpuPriority(HANDLE h, bool high);
bool GetGpuSchedulingPriority(HANDLE h, int* outPriority);
bool SetGpuSchedulingPriority(HANDLE h, int priority);

bool SetProcMemoryPriority(HANDLE h, int priority);
bool GetProcMemoryPriority(HANDLE h, int* out);

bool DisableMemoryCompression(SessionSnapshot& snapshot);
void RestoreMemoryCompression(SessionSnapshot& snapshot);

bool IsHwGpuSchedulingEnabled();
bool SetHwGpuScheduling(bool enable, SessionSnapshot& snapshot);

const wchar_t* PriorityClassName(PriorityClass pc);

bool BuildTopologyMask(int ccd, bool smt, DWORD_PTR sysMask, DWORD_PTR* mask, int* ideal);

}

#endif
