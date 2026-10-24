#pragma once
#ifndef KT_SETTINGS_H
#define KT_SETTINGS_H

#include <windows.h>
#include "config.h"

namespace kt {

struct GuiSettings {
    bool background_limiter = true;
    bool eco_qos_background = true;
    bool io_priority_background = true;
    bool thread_power_background_throttle = true;
    bool thread_power_game_unthrottle = true;
    bool dwm_mmcss = true;
    bool dwm_thread_priority = true;
    bool gpu_priority = true;
    bool io_priority_game_high = false;
    bool disable_c_states = false;
    bool suspend_gameinput_threads = false;
    bool high_performance_plan = true;
    bool turbo_boost = true;
    bool core_parking_disable = true;
    bool mmcss_game = false;
    bool page_priority_game = false;
    bool memory_priority_background = true;
    bool memory_priority_game_high = false;
    bool tournament_mode = false;
    bool background_cpu_pin = false;
    bool trim_background_ws = true;
    bool boost_input_threads = false;
    bool memory_compression_disable = false;
    bool gpu_hardware_scheduling = false;
    bool auto_profile_switching = false;
    bool dark_theme = true;
    bool start_minimized = false;
    bool auto_start = false;
    int  poll_interval_ms = 1000;
    int  background_cpu_pct = 20;
    std::wstring active_profile;
};

GuiSettings LoadSettings();

void SaveSetting(const wchar_t* name, DWORD value);

void SaveSettings(const GuiSettings& s);

void SaveProfile(const std::wstring& name);

void ApplySettingsToConfig(const GuiSettings& s, Config& cfg);

bool IsAutoStartEnabled();
void SetAutoStart(bool enable);

std::vector<std::wstring> LoadCustomGames();
void SaveCustomGames(const std::vector<std::wstring>& games);

}

#endif
