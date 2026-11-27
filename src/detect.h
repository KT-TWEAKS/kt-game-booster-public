#pragma once
#ifndef KT_DETECT_H
#define KT_DETECT_H

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include "config.h"

namespace kt {

bool IsSystemWhitelisted(const std::wstring& exeLower);
bool IsAntiCheatWhitelisted(const std::wstring& exeLower);
bool IsKernelAntiCheatGame(const std::wstring& exeLower);
bool IsAntiCheatProtectedGame(const std::wstring& exeLower);
int AntiCheatKind();
bool IsAntiCheatActive();
bool IsSecurityWhitelisted(const std::wstring& exeLower);
bool IsProtectedByConfig(const Config& cfg, const std::wstring& exeLower);

std::wstring MatchKnownGame(const std::wstring& exeLower);

std::wstring MatchGameName(const std::wstring& exeLower, const Config& cfg);

std::wstring MatchEpicGame(const std::wstring& exeLower, const std::wstring& pathLower);

const std::vector<std::wstring>& KnownGameNames();

bool HeuristicActiveWindow(DWORD fgPid, const std::wstring& exeLower);

bool HeuristicEngineModules(DWORD pid);

bool HeuristicGamePath(const std::wstring& pathLower);

bool HeuristicCommandLine(DWORD pid);

bool DetectSteamGames(const std::wstring& steamRoot, std::vector<std::wstring>& outGamePaths);

std::wstring GetProcPathByPid(DWORD pid);

std::vector<DWORD> FindPidsByName(const std::wstring& exeLower);

DWORD GetForegroundProcessId();

struct DetectionResult {
    std::wstring name;
    DWORD pid = 0;
    int confidence = 0;
    bool heuristic = false;
    std::string method;
};

struct ProcessSnapshot {
    struct Entry {
        DWORD pid;
        DWORD parentPid;
        std::wstring exeLower;
        std::wstring pathLower;
    };
    std::vector<Entry> processes;
    DWORD fgPid = 0;
    ULONGLONG timestamp = 0;
};

const ProcessSnapshot& GetCachedSnapshot();

std::vector<DetectionResult> DetectGamePidsEx(const Config& cfg,
                                               const std::wstring& forcedExe);

}

#endif
