// ReLimiter host API — implementation. See api.h for what this is for.
//
// THE REGISTRY IS THE WHOLE DESIGN. Every setting appears exactly once, in kSettings below, and both
// the enumeration a host builds its UI from and the get/set dispatch read that one table. Adding a
// setting to Config means adding one line here; nothing else in this file and nothing in any host
// changes. tools/check_api_registry.py fails the build if Config gains a field that this table does
// not, because the failure mode otherwise is silent: the setting simply never appears in a host UI
// and nobody notices for a release or two.
//
// Copyright (c) 2026 Mat, Laz, Rank. MIT — see LICENSE.

#include "api.h"
#include "config.h"

#ifndef RELIMITER_VERSION_STRING
#define RELIMITER_VERSION_STRING "unknown"
#endif

#include <cstring>
#include <string>

namespace {

static const char* const k_window_mode_choices[] = { "default", "borderless", "fullscreen" };
static const char* const k_vsync_mode_choices[] = { "game", "off", "on" };
static const char* const k_log_level_choices[] = { "error", "warn", "info", "debug" };
static const char* const k_smoothing_window_choices[] = { "medium", "dual" };

struct Entry {
    const char* key;
    const char* label;
    const char* group;
    const char* tooltip;
    uint32_t type;
    double min_value;
    double max_value;
    const char* const* choices;
    uint32_t choice_count;
};

// Order is the order a host will show them in, so it follows config.h.
const Entry kSettings[] = {
    { "target_fps", "Target FPS", "Core", "0 = stay below VRR ceiling", RELIMITER_TYPE_INT, 0, 1000, nullptr, 0 },
    { "enforcement_marker", "Enforcement Marker", "Core", "", RELIMITER_TYPE_STRING, 0, 0, nullptr, 0 },
    { "initial_wake_guard_us", "Initial Wake Guard (us)", "Wake Guard", "", RELIMITER_TYPE_DOUBLE, 0, 5000, nullptr, 0 },
    { "osd_enabled", "OSD Enabled", "OSD", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_x", "OSD X", "OSD", "0.0–1.0 screen percentage", RELIMITER_TYPE_FLOAT, 0, 1, nullptr, 0 },
    { "osd_y", "OSD Y", "OSD", "0.0–1.0 screen percentage", RELIMITER_TYPE_FLOAT, 0, 1, nullptr, 0 },
    { "osd_opacity", "OSD Opacity", "OSD", "", RELIMITER_TYPE_FLOAT, 0, 1, nullptr, 0 },
    { "osd_toggle_key", "OSD Toggle Key", "OSD", "", RELIMITER_TYPE_KEYBIND, 0, 0, nullptr, 0 },
    { "osd_preset_prev_key", "OSD Preset Prev Key", "OSD", "", RELIMITER_TYPE_KEYBIND, 0, 0, nullptr, 0 },
    { "osd_preset_next_key", "OSD Preset Next Key", "OSD", "", RELIMITER_TYPE_KEYBIND, 0, 0, nullptr, 0 },
    { "osd_position_cycle_key", "OSD Position Cycle Key", "OSD", "Cycle OSD through corners", RELIMITER_TYPE_KEYBIND, 0, 0, nullptr, 0 },
    { "osd_show_fps", "OSD Show FPS", "OSD element visibility", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_frametime", "OSD Show Frametime", "OSD element visibility", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_frametime_graph", "OSD Show Frametime Graph", "OSD element visibility", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_fg", "OSD Show FG", "OSD element visibility", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_limiter", "OSD Show Limiter", "OSD element visibility", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_pqi", "OSD Show PQI", "OSD element visibility", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_cpu_latency", "OSD Show CPU Latency", "OSD element visibility", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_pqi_breakdown", "OSD Show PQI Breakdown", "OSD element visibility", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_1pct_low", "OSD Show 1% low Low", "OSD element visibility", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_smoothness", "OSD Show Smoothness", "OSD element visibility", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_scale", "OSD Scale", "OSD appearance", "0.5 – 2.0 (50% – 200%)", RELIMITER_TYPE_FLOAT, 0.5, 2.0, nullptr, 0 },
    { "osd_drop_shadow", "OSD Drop Shadow", "OSD appearance", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_text_brightness", "OSD Text Brightness", "OSD appearance", "0.0 – 1.0", RELIMITER_TYPE_FLOAT, 0, 1, nullptr, 0 },
    { "window_mode", "Window Mode", "Window mode", "default | borderless | fullscreen", RELIMITER_TYPE_ENUM, 0, 0, k_window_mode_choices, 3 },
    { "fake_fullscreen", "Fake Fullscreen", "Fake Fullscreen", "Intercept exclusive fullscreen → borderless window", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "background_fps", "Background FPS", "Background", "", RELIMITER_TYPE_INT, 0, 1000, nullptr, 0 },
    { "fg_off_fps", "FG Off FPS", "Background", "0 = disabled, 30-360 = cap when FG not presenting", RELIMITER_TYPE_INT, 0, 360, nullptr, 0 },
    { "vsync_mode", "VSync Mode", "VSync Override", "game | off | on", RELIMITER_TYPE_ENUM, 0, 0, k_vsync_mode_choices, 3 },
    { "log_level", "Log Level", "Logging", "", RELIMITER_TYPE_ENUM, 0, 0, k_log_level_choices, 4 },
    { "csv_enabled", "CSV Enabled", "CSV Telemetry", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "reflex_inject", "Reflex Inject", "Reflex Injection", "Synthesize Reflex markers for non-Reflex games", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "flip_model_override", "Flip Model Override", "Flip Model Override (DX11)", "Force DXGI_SWAP_EFFECT_FLIP_DISCARD on DX11 bitblt swapchains", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "shared_presets", "Shared Presets", "Flip Model Override (DX11)", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "dynamic_mfg_passthrough", "Dynamic MFG Passthrough", "Flip Model Override (DX11)", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "dmfg_output_cap", "DMFG Output Cap", "Flip Model Override (DX11)", "", RELIMITER_TYPE_INT, 0, 1000, nullptr, 0 },
    { "adaptive_smoothing", "Adaptive Smoothing", "Adaptive Smoothing", "Enable P99-based adaptive smoothing (DX12+Reflex only)", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "smoothing_percentile", "Smoothing Percentile", "Adaptive Smoothing", "Target percentile (0.90–0.999)", RELIMITER_TYPE_DOUBLE, 0.9, 0.999, nullptr, 0 },
    { "smoothing_window", "Smoothing Window", "Adaptive Smoothing", "\"medium\" (256 frames) | \"dual\" (64+512)", RELIMITER_TYPE_ENUM, 0, 0, k_smoothing_window_choices, 2 },
    { "smoothing_bias_us", "Smoothing Bias (us)", "Adaptive Smoothing", "Constant bias added to computed offset (0–1000µs)", RELIMITER_TYPE_DOUBLE, 0, 1000, nullptr, 0 },
    { "osd_show_adaptive_smoothing", "OSD Show Adaptive Smoothing", "OSD: Adaptive Smoothing", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_0_1pct_low", "OSD Show 0.1% low 1% low Low", "OSD: Hardware monitoring", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_gpu_render_time", "OSD Show GPU Render Time", "OSD: Hardware monitoring", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_total_frame_cost", "OSD Show Total Frame Cost", "OSD: Hardware monitoring", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_fg_time", "OSD Show FG Time", "OSD: Hardware monitoring", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_gpu_temp", "OSD Show GPU Temp", "OSD: Hardware monitoring", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_gpu_clock", "OSD Show GPU Clock", "OSD: Hardware monitoring", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_gpu_usage", "OSD Show GPU Usage", "OSD: Hardware monitoring", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_gpu_power", "OSD Show GPU Power", "OSD: Hardware monitoring", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_vram", "OSD Show VRAM", "OSD: Hardware monitoring", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_cpu_usage", "OSD Show CPU Usage", "OSD: Hardware monitoring", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_ram", "OSD Show RAM", "OSD: Hardware monitoring", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_dlss_quality", "OSD Show DLSS Quality", "OSD: DLSS info", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_dlss_features", "OSD Show DLSS Features", "OSD: DLSS info", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_dlss_resolution", "OSD Show DLSS Resolution", "OSD: DLSS info", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_dlss_presets", "OSD Show DLSS Presets", "OSD: DLSS info", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "osd_show_dlss_versions", "OSD Show DLSS Versions", "OSD: DLSS info", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "blackout_key", "Blackout Key", "Monitor Blackout", "Keybind to toggle monitor blackout", RELIMITER_TYPE_KEYBIND, 0, 0, nullptr, 0 },
    { "oled_care_key", "OLED Care Key", "Monitor Blackout", "Keybind to toggle OLED Care (all-monitor blackout + 20fps)", RELIMITER_TYPE_KEYBIND, 0, 0, nullptr, 0 },
    { "oled_care_all_monitors", "OLED Care All Monitors", "Monitor Blackout", "true = black all monitors, false = only game monitor", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "oled_care_idle_minutes", "OLED Care Idle Minutes", "Monitor Blackout", "0 = disabled, >0 = auto-activate after N minutes of no input", RELIMITER_TYPE_INT, 0, 240, nullptr, 0 },
    { "selected_monitor", "Selected Monitor", "Monitor Blackout", "0 = default (no override), 1+ = monitor index", RELIMITER_TYPE_INT, 0, 16, nullptr, 0 },
    { "focus_lock", "Focus Lock", "Monitor Blackout", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "streamline_compat", "Streamline Compat", "Monitor Blackout", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
    { "dlss_info_hooks", "DLSS Info Hooks", "Monitor Blackout", "", RELIMITER_TYPE_BOOL, 0, 1, nullptr, 0 },
};

constexpr uint32_t kSettingCount = (uint32_t) (sizeof(kSettings) / sizeof(kSettings[0]));

const Entry* Find(const char* key) {
    if (!key) return nullptr;
    for (const Entry& e : kSettings)
        if (std::strcmp(e.key, key) == 0) return &e;
    return nullptr;
}

// Below is the one place a key is tied to its field, split by storage type because Config holds
// int, float, double and bool distinctly. A key lives in exactly one of them, so asking for a string
// key as a number returns 0 rather than a silently wrong value.

bool* BoolSlot(const char* key) {
    if (std::strcmp(key, "osd_enabled") == 0) return &g_config.osd_enabled;
    if (std::strcmp(key, "osd_show_fps") == 0) return &g_config.osd_show_fps;
    if (std::strcmp(key, "osd_show_frametime") == 0) return &g_config.osd_show_frametime;
    if (std::strcmp(key, "osd_show_frametime_graph") == 0) return &g_config.osd_show_frametime_graph;
    if (std::strcmp(key, "osd_show_fg") == 0) return &g_config.osd_show_fg;
    if (std::strcmp(key, "osd_show_limiter") == 0) return &g_config.osd_show_limiter;
    if (std::strcmp(key, "osd_show_pqi") == 0) return &g_config.osd_show_pqi;
    if (std::strcmp(key, "osd_show_cpu_latency") == 0) return &g_config.osd_show_cpu_latency;
    if (std::strcmp(key, "osd_show_pqi_breakdown") == 0) return &g_config.osd_show_pqi_breakdown;
    if (std::strcmp(key, "osd_show_1pct_low") == 0) return &g_config.osd_show_1pct_low;
    if (std::strcmp(key, "osd_show_smoothness") == 0) return &g_config.osd_show_smoothness;
    if (std::strcmp(key, "osd_drop_shadow") == 0) return &g_config.osd_drop_shadow;
    if (std::strcmp(key, "fake_fullscreen") == 0) return &g_config.fake_fullscreen;
    if (std::strcmp(key, "csv_enabled") == 0) return &g_config.csv_enabled;
    if (std::strcmp(key, "reflex_inject") == 0) return &g_config.reflex_inject;
    if (std::strcmp(key, "flip_model_override") == 0) return &g_config.flip_model_override;
    if (std::strcmp(key, "shared_presets") == 0) return &g_config.shared_presets;
    if (std::strcmp(key, "dynamic_mfg_passthrough") == 0) return &g_config.dynamic_mfg_passthrough;
    if (std::strcmp(key, "adaptive_smoothing") == 0) return &g_config.adaptive_smoothing;
    if (std::strcmp(key, "osd_show_adaptive_smoothing") == 0) return &g_config.osd_show_adaptive_smoothing;
    if (std::strcmp(key, "osd_show_0_1pct_low") == 0) return &g_config.osd_show_0_1pct_low;
    if (std::strcmp(key, "osd_show_gpu_render_time") == 0) return &g_config.osd_show_gpu_render_time;
    if (std::strcmp(key, "osd_show_total_frame_cost") == 0) return &g_config.osd_show_total_frame_cost;
    if (std::strcmp(key, "osd_show_fg_time") == 0) return &g_config.osd_show_fg_time;
    if (std::strcmp(key, "osd_show_gpu_temp") == 0) return &g_config.osd_show_gpu_temp;
    if (std::strcmp(key, "osd_show_gpu_clock") == 0) return &g_config.osd_show_gpu_clock;
    if (std::strcmp(key, "osd_show_gpu_usage") == 0) return &g_config.osd_show_gpu_usage;
    if (std::strcmp(key, "osd_show_gpu_power") == 0) return &g_config.osd_show_gpu_power;
    if (std::strcmp(key, "osd_show_vram") == 0) return &g_config.osd_show_vram;
    if (std::strcmp(key, "osd_show_cpu_usage") == 0) return &g_config.osd_show_cpu_usage;
    if (std::strcmp(key, "osd_show_ram") == 0) return &g_config.osd_show_ram;
    if (std::strcmp(key, "osd_show_dlss_quality") == 0) return &g_config.osd_show_dlss_quality;
    if (std::strcmp(key, "osd_show_dlss_features") == 0) return &g_config.osd_show_dlss_features;
    if (std::strcmp(key, "osd_show_dlss_resolution") == 0) return &g_config.osd_show_dlss_resolution;
    if (std::strcmp(key, "osd_show_dlss_presets") == 0) return &g_config.osd_show_dlss_presets;
    if (std::strcmp(key, "osd_show_dlss_versions") == 0) return &g_config.osd_show_dlss_versions;
    if (std::strcmp(key, "oled_care_all_monitors") == 0) return &g_config.oled_care_all_monitors;
    if (std::strcmp(key, "focus_lock") == 0) return &g_config.focus_lock;
    if (std::strcmp(key, "streamline_compat") == 0) return &g_config.streamline_compat;
    if (std::strcmp(key, "dlss_info_hooks") == 0) return &g_config.dlss_info_hooks;
    return nullptr;
}

int* IntSlot(const char* key) {
    if (std::strcmp(key, "target_fps") == 0) return &g_config.target_fps;
    if (std::strcmp(key, "background_fps") == 0) return &g_config.background_fps;
    if (std::strcmp(key, "fg_off_fps") == 0) return &g_config.fg_off_fps;
    if (std::strcmp(key, "dmfg_output_cap") == 0) return &g_config.dmfg_output_cap;
    if (std::strcmp(key, "oled_care_idle_minutes") == 0) return &g_config.oled_care_idle_minutes;
    if (std::strcmp(key, "selected_monitor") == 0) return &g_config.selected_monitor;
    return nullptr;
}

float* FloatSlot(const char* key) {
    if (std::strcmp(key, "osd_x") == 0) return &g_config.osd_x;
    if (std::strcmp(key, "osd_y") == 0) return &g_config.osd_y;
    if (std::strcmp(key, "osd_opacity") == 0) return &g_config.osd_opacity;
    if (std::strcmp(key, "osd_scale") == 0) return &g_config.osd_scale;
    if (std::strcmp(key, "osd_text_brightness") == 0) return &g_config.osd_text_brightness;
    return nullptr;
}

double* DoubleSlot(const char* key) {
    if (std::strcmp(key, "initial_wake_guard_us") == 0) return &g_config.initial_wake_guard_us;
    if (std::strcmp(key, "smoothing_percentile") == 0) return &g_config.smoothing_percentile;
    if (std::strcmp(key, "smoothing_bias_us") == 0) return &g_config.smoothing_bias_us;
    return nullptr;
}

std::string* StrSlot(const char* key) {
    if (std::strcmp(key, "enforcement_marker") == 0) return &g_config.enforcement_marker;
    if (std::strcmp(key, "osd_toggle_key") == 0) return &g_config.osd_toggle_key;
    if (std::strcmp(key, "osd_preset_prev_key") == 0) return &g_config.osd_preset_prev_key;
    if (std::strcmp(key, "osd_preset_next_key") == 0) return &g_config.osd_preset_next_key;
    if (std::strcmp(key, "osd_position_cycle_key") == 0) return &g_config.osd_position_cycle_key;
    if (std::strcmp(key, "window_mode") == 0) return &g_config.window_mode;
    if (std::strcmp(key, "vsync_mode") == 0) return &g_config.vsync_mode;
    if (std::strcmp(key, "log_level") == 0) return &g_config.log_level;
    if (std::strcmp(key, "smoothing_window") == 0) return &g_config.smoothing_window;
    if (std::strcmp(key, "blackout_key") == 0) return &g_config.blackout_key;
    if (std::strcmp(key, "oled_care_key") == 0) return &g_config.oled_care_key;
    return nullptr;
}

int ApiGetNumber(const char* key, double* out) {
    const Entry* e = Find(key);
    if (!e || !out) return 0;
    if (bool* b = BoolSlot(key))   { *out = *b ? 1.0 : 0.0; return 1; }
    if (int* i = IntSlot(key))     { *out = (double) *i;    return 1; }
    if (float* f = FloatSlot(key)) { *out = (double) *f;    return 1; }
    if (double* d = DoubleSlot(key)) { *out = *d;           return 1; }
    return 0;  // a string key asked for as a number
}

int ApiSetNumber(const char* key, double value) {
    const Entry* e = Find(key);
    if (!e) return 0;
    // Clamped here rather than trusted: a host is another program, and ValidateConfig only runs on
    // load. Entries with no meaningful range carry 0/0 and are left alone.
    if (e->min_value != e->max_value) {
        if (value < e->min_value) value = e->min_value;
        if (value > e->max_value) value = e->max_value;
    }
    if (bool* b = BoolSlot(key))   { *b = value != 0.0;        return 1; }
    if (int* i = IntSlot(key))     { *i = (int) value;         return 1; }
    if (float* f = FloatSlot(key)) { *f = (float) value;       return 1; }
    if (double* d = DoubleSlot(key)) { *d = value;             return 1; }
    return 0;
}

int ApiGetString(const char* key, char* buf, uint32_t buf_size) {
    if (!buf || buf_size == 0) return 0;
    buf[0] = '\0';
    std::string* s = StrSlot(key);
    if (!s) return 0;
    if (s->size() + 1 > buf_size) return 0;   // caller retries with a bigger buffer
    std::memcpy(buf, s->c_str(), s->size() + 1);
    return 1;
}

int ApiSetString(const char* key, const char* value) {
    const Entry* e = Find(key);
    std::string* s = StrSlot(key);
    if (!e || !s || !value) return 0;
    // An enum takes only its own choices. Anything else would put a value in the INI that
    // ValidateConfig throws away on the next load, which looks to a user like the setting not sticking.
    if (e->type == RELIMITER_TYPE_ENUM) {
        bool ok = false;
        for (uint32_t i = 0; i < e->choice_count; ++i)
            if (std::strcmp(e->choices[i], value) == 0) { ok = true; break; }
        if (!ok) return 0;
    }
    *s = value;
    return 1;
}

uint32_t ApiSettingCount(void) { return kSettingCount; }

int ApiDescribeSetting(uint32_t index, ReLimiterSettingInfo* out) {
    if (index >= kSettingCount || !out) return 0;
    // struct_size is the caller's, so a newer host reading an older build gets only the fields that
    // build knows about rather than a struct read past its end.
    if (out->struct_size < sizeof(ReLimiterSettingInfo)) return 0;
    const Entry& e = kSettings[index];
    out->key = e.key;
    out->label = e.label;
    out->group = e.group;
    out->tooltip = e.tooltip ? e.tooltip : "";
    out->type = e.type;
    out->min_value = e.min_value;
    out->max_value = e.max_value;
    out->choices = e.choices;
    out->choice_count = e.choice_count;
    return 1;
}

const char* ApiProductVersion(void) { return RELIMITER_VERSION_STRING; }

void ApiApply(void) { ApplyConfig(); }
void ApiSave(void)  { SaveConfig(); }

const ReLimiterApi kApi = {
    (uint32_t) sizeof(ReLimiterApi),
    RELIMITER_API_VERSION,
    ApiProductVersion,
    ApiSettingCount,
    ApiDescribeSetting,
    ApiGetNumber,
    ApiSetNumber,
    ApiGetString,
    ApiSetString,
    ApiApply,
    ApiSave,
};

}  // namespace

extern "C" __declspec(dllexport) const ReLimiterApi* ReLimiterGetApi(uint32_t requested_version) {
    // Only the version this build speaks. A host that asks for a newer one is told no rather than
    // handed a struct whose tail it will read as garbage.
    if (requested_version != RELIMITER_API_VERSION) return nullptr;
    return &kApi;
}
