#include "settings.h"
#include "util.h"
#include <vector>
#include <cstdio>

namespace kt {
namespace {
const wchar_t* kRegKey = L"Software\\KTGameBooster";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"KTGameBooster";
const wchar_t* kGamesValue = L"custom_games";
}

static DWORD RegGetDword(HKEY root, const wchar_t* key, const wchar_t* name, DWORD def) {
    HKEY h = nullptr;
    DWORD v = def, sz = sizeof(v), type = 0;
    if (RegOpenKeyExW(root, key, 0, KEY_READ, &h) == ERROR_SUCCESS) {
        if (RegQueryValueExW(h, name, nullptr, &type, (LPBYTE)&v, &sz) != ERROR_SUCCESS ||
            type != REG_DWORD)
            v = def;
        RegCloseKey(h);
    }
    return v;
}

static void RegSetDword(HKEY root, const wchar_t* key, const wchar_t* name, DWORD v) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(h, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(h);
    }
}

static bool RegGetBool(const wchar_t* name, bool def) {
    return RegGetDword(HKEY_CURRENT_USER, kRegKey, name, def ? 1 : 0) != 0;
}

static void RegSetBool(const wchar_t* name, bool v) {
    RegSetDword(HKEY_CURRENT_USER, kRegKey, name, v ? 1 : 0);
}

static std::wstring RegGetString(const wchar_t* name, const std::wstring& def) {
    HKEY h = nullptr;
    wchar_t buf[512];
    DWORD sz = sizeof(buf), type = 0;
    std::wstring out = def;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &h) == ERROR_SUCCESS) {
        if (RegQueryValueExW(h, name, nullptr, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS &&
            type == REG_SZ && sz >= 2)
            out = buf;
        RegCloseKey(h);
    }
    return out;
}

static void RegSetString(const wchar_t* name, const std::wstring& v) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &h,
                        nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(h, name, 0, REG_SZ, (const BYTE*)v.c_str(),
                       (DWORD)((v.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(h);
    }
}

GuiSettings LoadSettings() {
    GuiSettings s;
    s.background_limiter           = RegGetBool(L"background_limiter", s.background_limiter);
    s.eco_qos_background           = RegGetBool(L"eco_qos_background", s.eco_qos_background);
    s.io_priority_background       = RegGetBool(L"io_priority_background", s.io_priority_background);
    s.thread_power_background_throttle = RegGetBool(L"thread_power_background_throttle",
                                                    s.thread_power_background_throttle);
    s.thread_power_game_unthrottle = RegGetBool(L"thread_power_game_unthrottle",
                                                s.thread_power_game_unthrottle);
    s.dwm_mmcss                    = RegGetBool(L"dwm_mmcss", s.dwm_mmcss);
    s.dwm_thread_priority          = RegGetBool(L"dwm_thread_priority", s.dwm_thread_priority);
    s.gpu_priority                 = RegGetBool(L"gpu_priority", s.gpu_priority);
    s.io_priority_game_high        = RegGetBool(L"io_priority_game_high", s.io_priority_game_high);
    s.disable_c_states             = RegGetBool(L"disable_c_states", s.disable_c_states);
    s.suspend_gameinput_threads    = RegGetBool(L"suspend_gameinput_threads",
                                                s.suspend_gameinput_threads);
    s.high_performance_plan        = RegGetBool(L"high_performance_plan", s.high_performance_plan);
    s.turbo_boost                  = RegGetBool(L"turbo_boost", s.turbo_boost);
    s.core_parking_disable         = RegGetBool(L"core_parking_disable", s.core_parking_disable);
    s.mmcss_game                   = RegGetBool(L"mmcss_game", s.mmcss_game);
    s.page_priority_game           = RegGetBool(L"page_priority_game", s.page_priority_game);
    s.memory_priority_background   = RegGetBool(L"memory_priority_background",
                                                s.memory_priority_background);
    s.memory_priority_game_high    = RegGetBool(L"memory_priority_game_high",
                                                s.memory_priority_game_high);
    s.tournament_mode              = RegGetBool(L"tournament_mode", s.tournament_mode);
    s.background_cpu_pin           = RegGetBool(L"background_cpu_pin", s.background_cpu_pin);
    s.trim_background_ws           = RegGetBool(L"trim_background_ws", s.trim_background_ws);
    s.boost_input_threads          = RegGetBool(L"boost_input_threads", s.boost_input_threads);
    s.memory_compression_disable   = RegGetBool(L"memory_compression_disable", s.memory_compression_disable);
    s.gpu_hardware_scheduling      = RegGetBool(L"gpu_hardware_scheduling", s.gpu_hardware_scheduling);
    s.auto_profile_switching       = RegGetBool(L"auto_profile_switching", s.auto_profile_switching);
    s.start_minimized              = RegGetBool(L"start_minimized", s.start_minimized);
    s.auto_start                   = IsAutoStartEnabled();
    s.poll_interval_ms             = (int)RegGetDword(HKEY_CURRENT_USER, kRegKey, L"poll_interval_ms", 1000);
    s.background_cpu_pct           = (int)RegGetDword(HKEY_CURRENT_USER, kRegKey, L"background_cpu_pct", 20);
    s.active_profile               = RegGetString(L"active_profile", s.active_profile);
    if (s.poll_interval_ms < 100) s.poll_interval_ms = 100;
    s.dark_theme                   = RegGetBool(L"dark_theme", true);
    return s;
}

void SaveSetting(const wchar_t* name, DWORD value) {
    RegSetDword(HKEY_CURRENT_USER, kRegKey, name, value);
}

void SaveSettings(const GuiSettings& s) {
    RegSetBool(L"background_limiter", s.background_limiter);
    RegSetBool(L"eco_qos_background", s.eco_qos_background);
    RegSetBool(L"io_priority_background", s.io_priority_background);
    RegSetBool(L"thread_power_background_throttle", s.thread_power_background_throttle);
    RegSetBool(L"thread_power_game_unthrottle", s.thread_power_game_unthrottle);
    RegSetBool(L"dwm_mmcss", s.dwm_mmcss);
    RegSetBool(L"dwm_thread_priority", s.dwm_thread_priority);
    RegSetBool(L"gpu_priority", s.gpu_priority);
    RegSetBool(L"io_priority_game_high", s.io_priority_game_high);
    RegSetBool(L"disable_c_states", s.disable_c_states);
    RegSetBool(L"suspend_gameinput_threads", s.suspend_gameinput_threads);
    RegSetBool(L"high_performance_plan", s.high_performance_plan);
    RegSetBool(L"turbo_boost", s.turbo_boost);
    RegSetBool(L"core_parking_disable", s.core_parking_disable);
    RegSetBool(L"mmcss_game", s.mmcss_game);
    RegSetBool(L"page_priority_game", s.page_priority_game);
    RegSetBool(L"memory_priority_background", s.memory_priority_background);
    RegSetBool(L"memory_priority_game_high", s.memory_priority_game_high);
    RegSetBool(L"tournament_mode", s.tournament_mode);
    RegSetBool(L"background_cpu_pin", s.background_cpu_pin);
    RegSetBool(L"trim_background_ws", s.trim_background_ws);
    RegSetBool(L"boost_input_threads", s.boost_input_threads);
    RegSetBool(L"memory_compression_disable", s.memory_compression_disable);
    RegSetBool(L"gpu_hardware_scheduling", s.gpu_hardware_scheduling);
    RegSetBool(L"auto_profile_switching", s.auto_profile_switching);
    RegSetBool(L"dark_theme", s.dark_theme);
    RegSetBool(L"start_minimized", s.start_minimized);
    RegSetDword(HKEY_CURRENT_USER, kRegKey, L"poll_interval_ms", (DWORD)s.poll_interval_ms);
    RegSetDword(HKEY_CURRENT_USER, kRegKey, L"background_cpu_pct", (DWORD)s.background_cpu_pct);
    RegSetString(L"active_profile", s.active_profile);
}

void SaveProfile(const std::wstring& name) {
    RegSetString(L"active_profile", name);
}

void ApplySettingsToConfig(const GuiSettings& s, Config& cfg) {
    cfg.features.background_limiter = s.background_limiter;
    cfg.features.eco_qos_background = s.eco_qos_background;
    cfg.features.io_priority_background = s.io_priority_background;
    cfg.features.thread_power_background_throttle = s.thread_power_background_throttle;
    cfg.features.thread_power_game_unthrottle = s.thread_power_game_unthrottle;
    cfg.features.dwm_mmcss = s.dwm_mmcss;
    cfg.features.dwm_thread_priority = s.dwm_thread_priority;
    cfg.features.gpu_priority = s.gpu_priority;
    cfg.features.io_priority_game_high = s.io_priority_game_high;
    cfg.features.disable_c_states = s.disable_c_states;
    cfg.features.suspend_gameinput_threads = s.suspend_gameinput_threads;
    cfg.features.high_performance_plan = s.high_performance_plan;
    cfg.features.turbo_boost = s.turbo_boost;
    cfg.features.core_parking_disable = s.core_parking_disable;
    cfg.features.mmcss_game = s.mmcss_game;
    cfg.features.page_priority_game = s.page_priority_game;
    cfg.features.memory_priority_background = s.memory_priority_background;
    cfg.features.memory_priority_game_high = s.memory_priority_game_high;
    cfg.features.tournament_mode = s.tournament_mode;
    cfg.features.background_cpu_pin = s.background_cpu_pin;
    cfg.features.trim_background_ws = s.trim_background_ws;
    cfg.features.boost_input_threads = s.boost_input_threads;
    cfg.features.memory_compression_disable = s.memory_compression_disable;
    cfg.features.gpu_hardware_scheduling = s.gpu_hardware_scheduling;
    cfg.features.auto_profile_switching = s.auto_profile_switching;
    cfg.bgcpu.main_frame_cpu_pct = s.background_cpu_pct;
}

bool IsAutoStartEnabled() {
    HKEY h = nullptr;
    wchar_t buf[512];
    DWORD sz = sizeof(buf), type = 0;
    bool on = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &h) == ERROR_SUCCESS) {
        if (RegQueryValueExW(h, kRunValue, nullptr, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS &&
            type == REG_SZ)
            on = wcsstr(buf, L"KTGameBooster") != nullptr;
        RegCloseKey(h);
    }
    return on;
}

void SetAutoStart(bool enable) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_WRITE, nullptr, &h,
                        nullptr) != ERROR_SUCCESS)
        return;
    if (enable) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wchar_t cmd[MAX_PATH + 8];
        swprintf(cmd, MAX_PATH + 8, L"\"%s\" --start-minimized", exe);
        RegSetValueExW(h, kRunValue, 0, REG_SZ, (const BYTE*)cmd,
                       (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(h, kRunValue);
    }
    RegCloseKey(h);
}

std::vector<std::wstring> LoadCustomGames() {
    std::vector<std::wstring> out;
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &h) != ERROR_SUCCESS) return out;
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(h, kGamesValue, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        type == REG_MULTI_SZ && size >= 2) {
        std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, 0);
        if (RegQueryValueExW(h, kGamesValue, nullptr, &type, (LPBYTE)buf.data(),
                             &size) == ERROR_SUCCESS) {
            const wchar_t* p = buf.data();
            while (*p) {
                out.push_back(WLower(p));
                p += wcslen(p) + 1;
            }
        }
    }
    RegCloseKey(h);
    return out;
}

void SaveCustomGames(const std::vector<std::wstring>& games) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &h,
                        nullptr) != ERROR_SUCCESS)
        return;
    if (games.empty()) {
        RegDeleteValueW(h, kGamesValue);
    } else {
        std::vector<wchar_t> data;
        for (auto& g : games) {
            data.insert(data.end(), g.begin(), g.end());
            data.push_back(0);
        }
        data.push_back(0);
        RegSetValueExW(h, kGamesValue, 0, REG_MULTI_SZ, (const BYTE*)data.data(),
                       (DWORD)(data.size() * sizeof(wchar_t)));
    }
    RegCloseKey(h);
}

}
