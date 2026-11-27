#include "config.h"
#include "util.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>

namespace kt {

namespace {

std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::wstring StripComment(const std::wstring& line) {
    bool inStr = false;
    for (size_t i = 0; i < line.size(); i++) {
        wchar_t c = line[i];
        if (c == L'\"') inStr = !inStr;
        else if (c == L'#' && !inStr)
            return line.substr(0, i);
    }
    return line;
}

std::wstring ReadFileWide(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return L"";
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() >= 2 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE) {
        return std::wstring((const wchar_t*)(data.c_str() + 2), (data.size() - 2) / 2);
    }
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        data = data.substr(3);
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, data.c_str(), (int)data.size(), nullptr, 0);
    if (wlen <= 0) {
        wlen = MultiByteToWideChar(CP_ACP, 0, data.c_str(), (int)data.size(), nullptr, 0);
        if (wlen <= 0) return L"";
    }
    std::wstring out(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, data.c_str(), (int)data.size(), &out[0], wlen);
    return out;
}

std::wstring ParseStringLiteral(const std::wstring& raw, bool* ok) {
    std::wstring s = Trim(raw);
    if (ok) *ok = false;
    if (s.size() < 2 || s.front() != L'\"' || s.back() != L'\"') return L"";
    std::wstring body = s.substr(1, s.size() - 2);
    std::wstring out;
    for (size_t i = 0; i < body.size(); i++) {
        wchar_t c = body[i];
        if (c == L'\\' && i + 1 < body.size()) {
            wchar_t e = body[++i];
            switch (e) {
                case L'n': out += L'\n'; break;
                case L't': out += L'\t'; break;
                case L'r': out += L'\r'; break;
                case L'\"': out += L'\"'; break;
                case L'\\': out += L'\\'; break;
                case L'u': {
                    if (i + 4 < body.size()) {
                        std::wstring hex = body.substr(i + 1, 4);
                        wchar_t* end = nullptr;
                        unsigned int cp = (unsigned int)wcstoul(hex.c_str(), &end, 16);
                        if (end == hex.c_str() + 4) { out += (wchar_t)cp; i += 4; }
                        else out += L'u';
                    } else out += L'u';
                    break;
                }
                default: out += e; break;
            }
        } else {
            out += c;
        }
    }
    if (ok) *ok = true;
    return out;
}

enum class ValType { None, Str, Bool, Int, IntArray };

struct Val {
    ValType type = ValType::None;
    std::wstring str;
    bool b = false;
    long long i = 0;
    std::vector<int> ints;
};

Val ParseValue(const std::wstring& raw) {
    Val v;
    std::wstring s = Trim(raw);
    if (s.empty()) return v;
    if (s.front() == L'\"') {
        bool ok = false;
        v.str = ParseStringLiteral(s, &ok);
        if (ok) v.type = ValType::Str;
        return v;
    }
    if (s.front() == L'[') {
        std::wstring body = Trim(s.substr(1, s.size() - 2));
        v.type = ValType::IntArray;
        if (!body.empty()) {
            std::wstringstream ss(body);
            std::wstring tok;
            while (std::getline(ss, tok, L',')) {
                std::wstring t = Trim(tok);
                if (!t.empty()) v.ints.push_back((int)_wtoi(t.c_str()));
            }
        }
        return v;
    }
    std::wstring low = WLower(s);
    if (low == L"true") { v.type = ValType::Bool; v.b = true; return v; }
    if (low == L"false") { v.type = ValType::Bool; v.b = false; return v; }
    wchar_t* end = nullptr;
    unsigned long long iv;
    if (low.size() > 2 && low[0] == L'0' && low[1] == L'x')
        iv = wcstoull(s.c_str() + 2, &end, 16);
    else
        iv = wcstoull(s.c_str(), &end, 10);
    if (end && *end == 0 && end != s.c_str()) {
        v.type = ValType::Int;
        v.i = (long long)iv;
    }
    return v;
}

struct TomlLine {
    bool isHeader = false;
    bool isArrayHeader = false;
    std::wstring section;
    std::wstring key;
    std::wstring value;
};

Config ParseConfigText(const std::wstring& text) {
    Config cfg;
    std::wstring section;
    bool arrayMode = false;
    bool inProcArray = false;
    bool inThreadArray = false;
    bool inGameArray = false;
    bool inExclusionArray = false;

    std::wstringstream ss(text);
    std::wstring line;
    while (std::getline(ss, line)) {
        line = StripComment(line);
        line = Trim(line);
        if (line.empty()) continue;

        if (line.front() == L'[') {
            bool arr = line.size() >= 2 && line[1] == L'[';
            size_t close = arr ? line.rfind(L"]]") : line.rfind(L']');
            if (close == std::wstring::npos) continue;
            size_t start = arr ? 2 : 1;
            section = Trim(line.substr(start, close - start));
            arrayMode = arr;
            if (arr) {
                if (section == L"process") { inProcArray = true; inThreadArray = false; inGameArray = false; inExclusionArray = false; cfg.processes.push_back(ProcessRule()); }
                else if (section == L"threads") { inThreadArray = true; inProcArray = false; inGameArray = false; inExclusionArray = false; cfg.threads.push_back(ThreadRule()); }
                else if (section == L"game") { inGameArray = true; inProcArray = false; inThreadArray = false; inExclusionArray = false; }
                else if (section == L"exclusion") { inGameArray = false; inProcArray = false; inThreadArray = false; inExclusionArray = true; }
                else { inGameArray = false; inProcArray = false; inThreadArray = false; inExclusionArray = false; }
            } else {
                inProcArray = false; inThreadArray = false; inGameArray = false; inExclusionArray = false;
            }
            continue;
        }

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = Trim(line.substr(0, eq));
        std::wstring value = Trim(line.substr(eq + 1));
        Val v = ParseValue(value);

        auto setFlag = [&](bool& f) {
            if (v.type == ValType::Bool) f = v.b;
        };

        if (!arrayMode && section == L"config") {
            if (key == L"poll_interval_ms") { if (v.type == ValType::Int && v.i >= 50) cfg.poll_interval_ms = (int)v.i; }
        }
        else if (!arrayMode && section == L"features") {
            if      (key == L"dwm_mmcss") setFlag(cfg.features.dwm_mmcss);
            else if (key == L"dwm_thread_priority") setFlag(cfg.features.dwm_thread_priority);
            else if (key == L"gpu_priority") setFlag(cfg.features.gpu_priority);
            else if (key == L"background_limiter") setFlag(cfg.features.background_limiter);
            else if (key == L"thread_power_background_throttle") setFlag(cfg.features.thread_power_background_throttle);
            else if (key == L"thread_power_game_unthrottle") setFlag(cfg.features.thread_power_game_unthrottle);
            else if (key == L"io_priority_background") setFlag(cfg.features.io_priority_background);
            else if (key == L"io_priority_game_high") setFlag(cfg.features.io_priority_game_high);
            else if (key == L"eco_qos_background") setFlag(cfg.features.eco_qos_background);
            else if (key == L"suspend_gameinput_threads") setFlag(cfg.features.suspend_gameinput_threads);
            else if (key == L"disable_c_states") setFlag(cfg.features.disable_c_states);
            else if (key == L"disable_priority_boost") setFlag(cfg.features.disable_priority_boost);
            else if (key == L"background_cpu_pin") setFlag(cfg.features.background_cpu_pin);
            else if (key == L"boost_input_threads") setFlag(cfg.features.boost_input_threads);
            else if (key == L"high_performance_plan") setFlag(cfg.features.high_performance_plan);
            else if (key == L"turbo_boost") setFlag(cfg.features.turbo_boost);
            else if (key == L"core_parking_disable") setFlag(cfg.features.core_parking_disable);
            else if (key == L"mmcss_game") setFlag(cfg.features.mmcss_game);
            else if (key == L"page_priority_game") setFlag(cfg.features.page_priority_game);
            else if (key == L"memory_priority_background") setFlag(cfg.features.memory_priority_background);
            else if (key == L"memory_priority_game_high") setFlag(cfg.features.memory_priority_game_high);
            else if (key == L"trim_background_ws") setFlag(cfg.features.trim_background_ws);
            else if (key == L"tournament_mode") setFlag(cfg.features.tournament_mode);
            else if (key == L"memory_compression_disable") setFlag(cfg.features.memory_compression_disable);
            else if (key == L"gpu_hardware_scheduling") setFlag(cfg.features.gpu_hardware_scheduling);
            else if (key == L"auto_profile_switching") setFlag(cfg.features.auto_profile_switching);
            else if (key == L"gaming_power_plans") setFlag(cfg.features.gaming_power_plans);
            else if (key == L"game_tree_boost") setFlag(cfg.features.game_tree_boost);
            else if (key == L"pause_services_gaming") setFlag(cfg.features.pause_services_gaming);
            else if (key == L"anticheat_safe_mode") setFlag(cfg.features.anticheat_safe_mode);
            else if (key == L"game_dvr_disable") setFlag(cfg.features.game_dvr_disable);
            else if (key == L"fps_monitor") setFlag(cfg.features.fps_monitor);
            else if (key == L"power_plan_game_guid") {
                if (v.type == ValType::Str) wcsncpy_s(cfg.power_plan_game_guid, v.str.c_str(), _TRUNCATE);
            }
            else if (key == L"power_plan_off_guid") {
                if (v.type == ValType::Str) wcsncpy_s(cfg.power_plan_off_guid, v.str.c_str(), _TRUNCATE);
            }
        }
        else if (!arrayMode && section == L"background_cpu") {
            if      (key == L"auto_load") setFlag(cfg.bgcpu.auto_load);
            else if (key == L"main_frame_cpu_pct") { if (v.type == ValType::Int) cfg.bgcpu.main_frame_cpu_pct = (int)v.i; }
            else if (key == L"worker_cpu_pct")    { if (v.type == ValType::Int) cfg.bgcpu.worker_cpu_pct = (int)v.i; }
            else if (key == L"cpus") { if (v.type == ValType::IntArray) cfg.bgcpu.cpus = v.ints; }
        }
        else if (arrayMode && inGameArray) {
            if (key == L"exe") {
                if (v.type == ValType::Str) {
                    std::wstring e = WLower(v.str);
                    bool dup = false;
                    for (auto& g : cfg.custom_games) if (g == e) { dup = true; break; }
                    if (!dup && !e.empty()) cfg.custom_games.push_back(e);
                }
            }
        }
        else if (arrayMode && inExclusionArray) {
            if (key == L"exe" && v.type == ValType::Str) {
                std::wstring e = WLower(v.str);
                size_t bs = e.find_last_of(L"\\/");
                if (bs != std::wstring::npos) e = e.substr(bs + 1);
                bool dup = false;
                for (auto& g : cfg.limiter_exclusions) if (g == e) { dup = true; break; }
                if (!dup && !e.empty()) cfg.limiter_exclusions.push_back(e);
            }
        }
        else if (arrayMode && inProcArray && !cfg.processes.empty()) {
            ProcessRule& r = cfg.processes.back();
            if (key == L"exe") { if (v.type == ValType::Str) r.exe = WLower(v.str); }
            else if (key == L"path") { if (v.type == ValType::Str) r.path = v.str; }
            else if (key == L"priority_class") {
                if (v.type == ValType::Str) {
                    std::wstring s = WLower(v.str);
                    if (s == L"idle") { r.priority = PriorityClass::Idle; r.has_priority = true; }
                    else if (s == L"below_normal") { r.priority = PriorityClass::BelowNormal; r.has_priority = true; }
                    else if (s == L"normal") { r.priority = PriorityClass::Normal; r.has_priority = true; }
                    else if (s == L"above_normal") { r.priority = PriorityClass::AboveNormal; r.has_priority = true; }
                    else if (s == L"high") { r.priority = PriorityClass::High; r.has_priority = true; }
                    else if (s == L"realtime") { r.priority = PriorityClass::Realtime; r.has_priority = true; }
                }
            }
            else if (key == L"action") {
                if (v.type == ValType::Str) {
                    std::wstring s = WLower(v.str);
                    if (s == L"suspend") { r.action = Action::Suspend; r.has_action = true; }
                    else if (s == L"idle") { r.action = Action::Idle; r.has_action = true; }
                    else if (s == L"below_normal") { r.action = Action::BelowNormal; r.has_action = true; }
                    else if (s == L"lowest") { r.action = Action::Lowest; r.has_action = true; }
                }
            }
            else if (key == L"affinity_cores") { if (v.type == ValType::IntArray) { r.affinity_cores = v.ints; r.has_affinity_cores = true; } }
            else if (key == L"affinity_ccd") { if (v.type == ValType::Int) r.affinity_ccd = (int)v.i; }
            else if (key == L"affinity_smt") setFlag(r.affinity_smt);
            else if (key == L"ideal_processor") { if (v.type == ValType::Int) { r.ideal_processor = (int)v.i; r.has_ideal = true; } }
            else if (key == L"eco_qos") { if (v.type == ValType::Bool) { r.has_eco_qos = true; r.eco_qos = v.b; } }
            else if (key == L"io_priority") { if (v.type == ValType::Bool) { r.has_io_priority = true; r.io_priority = v.b; } }
            else if (key == L"profile") { if (v.type == ValType::Str) r.profile = WLower(v.str); }
        }
        else if (arrayMode && inThreadArray && !cfg.threads.empty()) {
            ThreadRule& r = cfg.threads.back();
            if (key == L"thread_name") { if (v.type == ValType::Str) r.name = v.str; }
            else if (key == L"priority") {
                if (v.type == ValType::Str) {
                    std::wstring s = WLower(v.str);
                    if (s == L"time_critical") r.priority = ThreadPrio::TimeCritical;
                    else if (s == L"highest") r.priority = ThreadPrio::Highest;
                    else if (s == L"above_normal") r.priority = ThreadPrio::AboveNormal;
                    else if (s == L"normal") r.priority = ThreadPrio::Normal;
                    else if (s == L"below_normal") r.priority = ThreadPrio::BelowNormal;
                    else if (s == L"lowest") r.priority = ThreadPrio::Lowest;
                    else if (s == L"idle") r.priority = ThreadPrio::Idle;
                }
            }
            else if (key == L"action") {
                if (v.type == ValType::Str) {
                    std::wstring s = WLower(v.str);
                    if (s == L"suspend") r.action = Action::Suspend;
                    else if (s == L"idle") r.action = Action::Idle;
                    else if (s == L"below_normal") r.action = Action::BelowNormal;
                    else if (s == L"lowest") r.action = Action::Lowest;
                }
            }
        }
        else if (!arrayMode && section == L"process") {
            ProcessRule r;
            if (key == L"exe") { if (v.type == ValType::Str) r.exe = WLower(v.str); }
            else if (key == L"path") { if (v.type == ValType::Str) r.path = v.str; }
            else if (key == L"priority_class") {
                if (v.type == ValType::Str) {
                    std::wstring s = WLower(v.str);
                    if (s == L"high") { r.priority = PriorityClass::High; r.has_priority = true; }
                    else if (s == L"realtime") { r.priority = PriorityClass::Realtime; r.has_priority = true; }
                }
            }
            if (!r.exe.empty()) cfg.processes.push_back(r);
        }
    }
    return cfg;
}

}

const wchar_t* EmbeddedDefaultConfig() {
    static const wchar_t* s =         L"# KT Game Booster - default gamemode.toml (embedded)\n"
        L"[config]\n"
        L"poll_interval_ms = 1000\n"
        L"\n"
        L"[features]\n"
        L"dwm_mmcss = true\n"
        L"dwm_thread_priority = true\n"
        L"gpu_priority = true\n"
        L"background_limiter = true\n"
        L"thread_power_background_throttle = true\n"
        L"thread_power_game_unthrottle = true\n"
        L"io_priority_background = true\n"
        L"io_priority_game_high = true\n"
        L"eco_qos_background = true\n"
        L"suspend_gameinput_threads = false\n"
        L"disable_c_states = false\n"
        L"disable_priority_boost = true\n"
        L"background_cpu_pin = false\n"
        L"boost_input_threads = false\n"
        L"# mega update: power / scheduling knobs (user-mode, no signed driver)\n"
        L"high_performance_plan = true\n"
        L"turbo_boost = true\n"
        L"core_parking_disable = true\n"
        L"mmcss_game = true\n"
        L"page_priority_game = true\n"
        L"memory_priority_background = true\n"
        L"memory_priority_game_high = true\n"
        L"trim_background_ws = true\n"
        L"tournament_mode = false\n"
        L"memory_compression_disable = false\n"
        L"gpu_hardware_scheduling = false\n"
        L"auto_profile_switching = false\n"
        L"boost_input_threads = true\n"
        L"gaming_power_plans = false\n"
        L"# Boost na arvore do jogo + servicos\n"
        L"game_tree_boost = true\n"
        L"pause_services_gaming = false\n"
        L"# Seguranca anticheat: jogos com AC de kernel rodam so com levers\n"
        L"# user-mode seguros (suspensao/servicos/limiter saem da sessao)\n"
        L"anticheat_safe_mode = true\n"
        L"# Alavancas de FPS (Game DVR capture off + monitor de FPS real)\n"
        L"game_dvr_disable = true\n"
        L"fps_monitor = true\n"
        L"power_plan_game_guid = \"\"\n"
        L"power_plan_off_guid = \"\"\n"
        L"\n"
        L"[background_cpu]\n"
        L"auto_load = false\n"
        L"main_frame_cpu_pct = 20\n"
        L"worker_cpu_pct = 3\n"
        L"cpus = []\n"
        L"\n"
        L"# Processos que o limitador de fundo NUNCA deve tocar\n"
        L"# (ex.: musica/chat/captura continuam normais durante o jogo)\n"
        L"[[exclusion]]\n"
        L"exe = \"spotify.exe\"\n"
        L"[[exclusion]]\n"
        L"exe = \"discord.exe\"\n"
        L"[[exclusion]]\n"
        L"exe = \"obs64.exe\"\n"
        L"\n"
        L"# Extra games (also editable from the GUI - game picker)\n"
        L"# [[game]]\n"
        L"# exe = \"mygame.exe\"\n"
        L"\n"
        L"# Game targets\n"
        L"[[process]]\n"
        L"exe = \"cs2.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"FortniteClient-Win64-Shipping.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"RocketLeague.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"VALORANT-Win64-Shipping.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"apexlegends.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"overwatch2.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"valorant.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"r5apex.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"GTA5.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"GTA5_enhanced.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"RDR2.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Cyberpunk2077.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"eldenring.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"DarkSoulsIII.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Sekiro.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Starfield.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"HogwartsLegacy.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"SpiderMan.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"SpiderManPC.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"MilesMorales.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"GodOfWar.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"HorizonZeroDawn.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"HorizonForbiddenWest.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"LastOfUsPart1.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Uncharted.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Returnal.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"RainbowSix.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"RainbowSix_Vulkan.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"TheDivision2.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"ForzaHorizon5.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"ForzaMotorsport.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"HaloInfinite.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Warframe.x64.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Warhammer40k.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Diablo IV.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Wow.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"LostArk.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"NewWorld.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"StarWarsOutlaws.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"ACMirage.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"ACShadows.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"FarCry6.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"WatchDogsLegion.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"GhostOfTsushima.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Palworld.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"OnceHuman.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"TheForeverWinter.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Satisfactory-Win64-Shipping.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"LethalCompany.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"League of Legends.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"dota2.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Smite.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Deadlock.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"BaldursGate3.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"ManorLords.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"msfs.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"EuroTruck2.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"AmericanTruck.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"Minecraft.Windows10.exe\"\n"
        L"priority_class = \"high\"\n"
        L"[[process]]\n"
        L"exe = \"javaw.exe\"\n"
        L"priority_class = \"high\"\n"
        L"\n"
        L"# Protected background processes (never throttled)\n"
        L"[[process]]\n"
        L"exe = \"ApplicationFrameHost.exe\"\n"
        L"[[process]]\n"
        L"exe = \"brave.exe\"\n"
        L"[[process]]\n"
        L"exe = \"chrome.exe\"\n"
        L"[[process]]\n"
        L"exe = \"discord.exe\"\n"
        L"[[process]]\n"
        L"exe = \"Dropbox.exe\"\n"
        L"[[process]]\n"
        L"exe = \"EpicWebHelper.exe\"\n"
        L"[[process]]\n"
        L"exe = \"explorer.exe\"\n"
        L"[[process]]\n"
        L"exe = \"firefox.exe\"\n"
        L"[[process]]\n"
        L"exe = \"GameInputRedistService.exe\"\n"
        L"[[process]]\n"
        L"exe = \"GameInputSvc.exe\"\n"
        L"[[process]]\n"
        L"exe = \"gamingservices.exe\"\n"
        L"[[process]]\n"
        L"exe = \"gamingservicesnet.exe\"\n"
        L"[[process]]\n"
        L"exe = \"GoogleDriveFS.exe\"\n"
        L"[[process]]\n"
        L"exe = \"msedge.exe\"\n"
        L"[[process]]\n"
        L"exe = \"MsMpEng.exe\"\n"
        L"[[process]]\n"
        L"exe = \"OneDrive.exe\"\n"
        L"[[process]]\n"
        L"exe = \"SearchIndexer.exe\"\n"
        L"[[process]]\n"
        L"exe = \"SgrmBroker.exe\"\n"
        L"[[process]]\n"
        L"exe = \"Slack.exe\"\n"
        L"[[process]]\n"
        L"exe = \"steam.exe\"\n"
        L"[[process]]\n"
        L"exe = \"steamservice.exe\"\n"
        L"[[process]]\n"
        L"exe = \"steamwebhelper.exe\"\n"
        L"[[process]]\n"
        L"exe = \"svchost.exe\"\n"
        L"[[process]]\n"
        L"exe = \"Teams.exe\"\n"
        L"[[process]]\n"
        L"exe = \"TextInputHost.exe\"\n"
        L"[[process]]\n"
        L"exe = \"WUDFHost.exe\"\n"
        L"\n"
        L"# Thread optimization rules (patterns, * = wildcard)\n"
        L"[[threads]]\n"
        L"thread_name = \"GameThread\"\n"
        L"priority = \"time_critical\"\n"
        L"[[threads]]\n"
        L"thread_name = \"RenderThread*\"\n"
        L"priority = \"highest\"\n"
        L"[[threads]]\n"
        L"thread_name = \"RHIThread\"\n"
        L"priority = \"highest\"\n"
        L"[[threads]]\n"
        L"thread_name = \"RHISubmissionThread\"\n"
        L"priority = \"highest\"\n"
        L"[[threads]]\n"
        L"thread_name = \"RHIInterruptThread\"\n"
        L"priority = \"highest\"\n"
        L"[[threads]]\n"
        L"thread_name = \"D3D11RenderThread*\"\n"
        L"priority = \"highest\"\n"
        L"[[threads]]\n"
        L"thread_name = \"D3D12RenderThread*\"\n"
        L"priority = \"highest\"\n"
        L"[[threads]]\n"
        L"thread_name = \"VulkanThread*\"\n"
        L"priority = \"highest\"\n"
        L"[[threads]]\n"
        L"thread_name = \"DX12Renderer*\"\n"
        L"priority = \"highest\"\n"        L"[[threads]]\n"
        L"thread_name = \"*AudioMixerRenderThread*\"\n"
        L"priority = \"above_normal\"\n"
        L"[[threads]]\n"
        L"thread_name = \"WindowsRawInputThread\"\n"
        L"priority = \"time_critical\"\n"
        L"[[threads]]\n"
        L"thread_name = \"SDLRawInput*\"\n"
        L"priority = \"time_critical\"\n"
        L"[[threads]]\n"
        L"thread_name = \"MainThread*\"\n"
        L"priority = \"time_critical\"\n"
        L"[[threads]]\n"
        L"thread_name = \"AudioThread*\"\n"
        L"priority = \"above_normal\"\n"
        L"[[threads]]\n"
        L"thread_name = \"*Async Shader Compile*\"\n"
        L"priority = \"normal\"\n"
        L"[[threads]]\n"
        L"thread_name = \"*Background Worker*\"\n"
        L"action = \"below_normal\"\n"
        L"[[threads]]\n"
        L"thread_name = \"*BackgroundThreadPool*\"\n"
        L"action = \"below_normal\"\n"
        L"[[threads]]\n"
        L"thread_name = \"*CHTTP*ThreadPool*\"\n"
        L"action = \"below_normal\"\n"
        L"[[threads]]\n"
        L"thread_name = \"AK*\"\n"
        L"action = \"below_normal\"\n"
        L"[[threads]]\n"
        L"thread_name = \"IoService*\"\n"
        L"action = \"below_normal\"\n"
        L"[[threads]]\n"
        L"thread_name = \"UnityWorker*\"\n"
        L"action = \"below_normal\"\n"
        L"[[threads]]\n"
        L"thread_name = \"ThreadPoolTaskScheduler*\"\n"
        L"action = \"below_normal\"\n";
    return s;
}

Config LoadConfig() {
    Config cfg = LoadConfigFrom(L"gamemode.toml");
    if (cfg.loaded_from_file) return cfg;
    if (GetFileAttributesW(L"profiles\\background.toml") != INVALID_FILE_ATTRIBUTES) {
        std::wstring bg = ReadFileWide(L"profiles\\background.toml");
        if (!bg.empty()) {
            Log(L"config", L"profiles\\background.toml loaded");
            cfg = ParseConfigText(bg);
            cfg.loaded_from_file = true;
            cfg.file_path = L"profiles\\background.toml";
            return cfg;
        }
    }
    Log(L"config", L"gamemode.toml not found, using embedded config");
    return cfg;
}

Config LoadConfigFrom(const std::wstring& path) {
    Config cfg;
    std::wstring text = ReadFileWide(path);
    if (!text.empty()) {
        cfg = ParseConfigText(text);
        cfg.loaded_from_file = true;
        cfg.file_path = path;
        Log(L"config", L"%s loaded", path.c_str());
        return cfg;
    }
    cfg = ParseConfigText(EmbeddedDefaultConfig());
    return cfg;
}

std::vector<std::wstring> ListProfiles(const std::wstring& dir) {
    std::vector<std::wstring> out;
    std::wstring pattern = dir.empty() ? L"*.toml" : dir + L"\\*.toml";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fd.cFileName;
        size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos) name = name.substr(0, dot);
        out.push_back(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

}
