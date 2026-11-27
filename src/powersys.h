#pragma once
#ifndef KT_POWERSYS_H
#define KT_POWERSYS_H

#include <windows.h>
#include <string>
#include <vector>

namespace kt {

class SessionSnapshot;

bool SavePowerState();
bool SetHighPerformancePower();
bool RestorePowerState();

bool ApplyMaxBoost(bool turboBoost, bool coreParkingOff, SessionSnapshot& snapshot);
bool RestoreMaxBoost();

bool DisableCStates(SessionSnapshot& snapshot);
bool RestoreCStates();

bool EnableDwmMmcss();
void DisableDwmMmcss();

void BoostDwmThreads(SessionSnapshot& snapshot);
void RestoreDwmThreads();

bool ApplyGamingPowerPlan(const GUID& gamePlan);
bool RestoreGamingPowerPlan();

bool DisableGameDvr(SessionSnapshot& snapshot);
void RestoreGameDvr();

bool RevertExtremeShell(const std::vector<DWORD>& winlogonPids, SessionSnapshot& snapshot);
bool RecoverExtremeShell();

bool GetProcessCpuSets(HANDLE hProcess, std::vector<DWORD>* setIds);
bool SetProcessCpuSets(HANDLE hProcess, const std::vector<DWORD>& setIds);

}

#endif
