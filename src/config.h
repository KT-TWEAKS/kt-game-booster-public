#pragma once
#ifndef KT_CONFIG_H
#define KT_CONFIG_H

#include <string>
#include <vector>
#include <windows.h>

namespace kt {

enum class PriorityClass { None, Idle, BelowNormal, Normal, AboveNormal, High, Realtime };
enum class ThreadPrio   { None, TimeCritical, Highest, AboveNormal, Normal, BelowNormal, Lowest, Idle };
enum class Action       { None, Suspend, Idle, BelowNormal, Lowest };

struct ProcessRule {
    std::wstring exe;
    std::wstring path;
    bool         has_priority = false;
    PriorityClass priority = PriorityClass::None;
    bool         has_action = false;
    Action       action = Action::None;
    bool              has_affinity_cores = false;
    std::vector<int>  affinity_cores;
    int               affinity_ccd = -1;
    bool              affinity_smt = true;
    bool              has_ideal = false;
    int               ideal_processor = -1;
    bool              has_eco_qos = false;
    bool              eco_qos = false;
    bool              has_io_priority = false;
    bool              io_priority = false;
    std::wstring      profile;
    bool whitelist_only() const {
        return !has_priority && !has_action && !has_affinity_cores &&
               !has_ideal && !has_eco_qos && !has_io_priority &&
               profile.empty();
    }
};

struct ThreadRule {
    std::wstring name;
    ThreadPrio   priority = ThreadPrio::None;
    Action       action = Action::None;
};

struct Features {
    bool dwm_mmcss = true;
    bool dwm_thread_priority = true;
    bool gpu_priority = true;
    bool background_limiter = true;
    bool thread_power_background_throttle = true;
    bool thread_power_game_unthrottle = true;
    bool io_priority_background = true;
    bool io_priority_game_high = true;
    bool eco_qos_background = true;
    bool suspend_gameinput_threads = false;
    bool disable_c_states = false;
    bool disable_priority_boost = true;
    bool memory_lock = false;
    bool background_cpu_pin = false;
    bool boost_input_threads = false;
    bool high_performance_plan = true;
    bool turbo_boost = true;
    bool core_parking_disable = true;
    bool mmcss_game = true;
    bool page_priority_game = true;
    bool memory_priority_background = true;
    bool memory_priority_game_high = true;
    bool pin_perf_cores = false;
    bool trim_background_ws = true;
    bool tournament_mode = false;
    bool network_throttling_disable = false;
    bool memory_compression_disable = false;
    bool gpu_hardware_scheduling = false;
    bool auto_profile_switching = false;
    bool timer_resolution_500us = false;
    bool cpu_sets_pin = false;
    bool kill_shell_processes = false;
    bool gaming_power_plans = false;
    bool extreme_shell_mode = false;
    bool extreme_kill_dwm = false;
    bool auto_clean_ram = false;
    bool game_tree_boost = true;
    bool pause_services_gaming = false;
    bool anticheat_safe_mode = true;
    bool game_dvr_disable = true;
    bool fso_disable = false;
    bool fps_monitor = true;
};

struct BackgroundCpu {
    bool              auto_load = false;
    int               main_frame_cpu_pct = 20;
    int               worker_cpu_pct = 3;
    std::vector<int>  cpus;
};

struct Config {
    Features       features;
    BackgroundCpu  bgcpu;
    std::vector<ProcessRule> processes;
    std::vector<ThreadRule>  threads;
    std::vector<std::wstring> custom_games;
    std::vector<std::wstring> limiter_exclusions;
    int            poll_interval_ms = 1000;
    bool           loaded_from_file = false;
    std::wstring   file_path;
    wchar_t        power_plan_game_guid[40]{};
    wchar_t        power_plan_off_guid[40]{};
};

Config LoadConfig();

Config LoadConfigFrom(const std::wstring& path);

std::vector<std::wstring> ListProfiles(const std::wstring& dir);

const wchar_t* EmbeddedDefaultConfig();

}

#endif
