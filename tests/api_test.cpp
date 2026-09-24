// Exercises the host API against the real registry. Runs anywhere: the only Windows in api.cpp is
// __declspec on the export, and Config is plain data, so the pieces that could actually be wrong --
// the 65 registry entries, the key-to-field wiring, clamping, enum validation and the string buffer
// contract -- are all testable off Windows.
#include "../src/api.h"
#include "../src/config.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

// Stand-ins for the parts of ReLimiter this API touches.
Config g_config;
static int g_applied = 0, g_saved = 0;
void ApplyConfig() { ++g_applied; }
void SaveConfig()  { ++g_saved; }
void ValidateConfig() {}
void LoadConfig(HMODULE) {}

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main() {
    const ReLimiterApi* api = ReLimiterGetApi(RELIMITER_API_VERSION);
    CHECK(api != nullptr, "the current version is served");
    // A version this build does not speak is refused, not served a struct whose tail the caller
    // would read as garbage.
    CHECK(ReLimiterGetApi(RELIMITER_API_VERSION + 1) == nullptr, "a newer version is refused");
    CHECK(ReLimiterGetApi(0) == nullptr, "version 0 is refused");
    if (!api) return 1;

    CHECK(api->struct_size == sizeof(ReLimiterApi), "struct_size is this build's");
    CHECK(api->setting_count() > 50, "the registry is populated");

    // Every entry is well formed, and no key appears twice -- a duplicate would make one of them
    // permanently unreachable through get/set.
    std::set<std::string> keys;
    bool sawEnum = false, sawKeybind = false, sawBool = false;
    for (uint32_t i = 0; i < api->setting_count(); ++i) {
        ReLimiterSettingInfo info {};
        info.struct_size = sizeof(info);
        CHECK(api->describe_setting(i, &info) == 1, "describe_setting succeeds in range");
        CHECK(info.key && *info.key, "key is non-empty");
        CHECK(info.label && *info.label, "label is non-empty");
        CHECK(info.group && *info.group, "group is non-empty");
        CHECK(info.tooltip != nullptr, "tooltip is never null");
        CHECK(info.type >= RELIMITER_TYPE_BOOL && info.type <= RELIMITER_TYPE_KEYBIND, "type is known");
        CHECK(keys.insert(info.key).second, "key is unique");
        if (info.type == RELIMITER_TYPE_ENUM) {
            sawEnum = true;
            CHECK(info.choices && info.choice_count > 0, "an enum carries its choices");
        } else {
            CHECK(info.choices == nullptr, "only an enum carries choices");
        }
        if (info.type == RELIMITER_TYPE_KEYBIND) sawKeybind = true;
        if (info.type == RELIMITER_TYPE_BOOL) {
            sawBool = true;
            CHECK(info.min_value == 0 && info.max_value == 1, "a bool's range is 0..1");
        }
    }
    CHECK(sawEnum, "enums are classified");
    CHECK(sawKeybind, "keybinds are classified");
    CHECK(sawBool, "bools are classified");

    // Out of range, and a caller passing a smaller struct than this build writes.
    ReLimiterSettingInfo info {};
    info.struct_size = sizeof(info);
    CHECK(api->describe_setting(api->setting_count(), &info) == 0, "out of range fails");
    // NOT named `small`: rpcndr.h, via Windows.h, does `#define small char`, so a variable of that
    // name becomes `ReLimiterSettingInfo char {}` and MSVC rejects it. A stubbed Windows.h off-platform
    // does not define it, which is exactly the false confidence a stub buys.
    ReLimiterSettingInfo undersized {};
    undersized.struct_size = 4;
    CHECK(api->describe_setting(0, &undersized) == 0, "an undersized struct is refused");

    // Numbers reach the real fields, in both directions and for each storage type.
    double v = -1;
    g_config.osd_enabled = true;
    CHECK(api->get_number("osd_enabled", &v) == 1 && v == 1.0, "bool reads as 1");
    CHECK(api->set_number("osd_enabled", 0) == 1 && g_config.osd_enabled == false, "bool writes");
    CHECK(api->set_number("target_fps", 141) == 1 && g_config.target_fps == 141, "int writes");
    CHECK(api->set_number("osd_scale", 1.25) == 1 && g_config.osd_scale > 1.24f, "float writes");
    CHECK(api->set_number("smoothing_bias_us", 250.5) == 1 && g_config.smoothing_bias_us == 250.5, "double writes");

    // Clamped, because a host is another program and ValidateConfig only runs on load.
    CHECK(api->set_number("osd_scale", 99) == 1 && g_config.osd_scale == 2.0f, "over range clamps to max");
    CHECK(api->set_number("osd_opacity", -5) == 1 && g_config.osd_opacity == 0.0f, "under range clamps to min");
    // An entry with no meaningful range is left alone rather than clamped to 0.
    CHECK(api->set_number("initial_wake_guard_us", 1200) == 1 && g_config.initial_wake_guard_us == 1200.0, "in-range value is kept");

    // Unknown keys, and a string key asked for as a number.
    CHECK(api->get_number("not_a_setting", &v) == 0, "unknown key fails");
    CHECK(api->set_number("not_a_setting", 1) == 0, "unknown key does not write");
    CHECK(api->get_number("window_mode", &v) == 0, "a string is not readable as a number");

    // Strings, enums and the buffer contract.
    char buf[64];
    g_config.window_mode = "borderless";
    CHECK(api->get_string("window_mode", buf, sizeof(buf)) == 1 && std::strcmp(buf, "borderless") == 0, "string reads");
    CHECK(api->set_string("window_mode", "fullscreen") == 1 && g_config.window_mode == "fullscreen", "a valid choice writes");
    // An invalid choice is refused rather than written: ValidateConfig would throw it away on the next
    // load, which to a user looks like the setting silently not sticking.
    CHECK(api->set_string("window_mode", "windowed") == 0 && g_config.window_mode == "fullscreen", "an invalid choice is refused");
    CHECK(api->set_string("log_level", "debug") == 1 && g_config.log_level == "debug", "log_level takes its choices");
    CHECK(api->set_string("log_level", "verbose") == 0, "log_level refuses others");
    // A keybind is a free string, not an enum.
    CHECK(api->set_string("osd_toggle_key", "Ctrl+F12") == 1 && g_config.osd_toggle_key == "Ctrl+F12", "a keybind takes any string");

    // Too small a buffer fails and still leaves a terminated string, so a caller can retry.
    char tiny[4];
    g_config.window_mode = "borderless";
    CHECK(api->get_string("window_mode", tiny, sizeof(tiny)) == 0, "a short buffer fails");
    CHECK(tiny[0] == '\0', "a failed read leaves an empty string, not a truncated one");
    CHECK(api->get_string("window_mode", buf, 0) == 0, "a zero-length buffer fails");
    CHECK(api->get_string("target_fps", buf, sizeof(buf)) == 0, "a number is not readable as a string");

    // ── The special zero, which is what makes an fps slider honest ──
    //
    // target_fps = 0 does not mean "0 fps" or "no limit", it means "stay below the VRR ceiling", and
    // ValidateConfig clamps anything else to 30..1000. A host that reads min_value as the bottom of a
    // slider offers 1..29 fps, all of which ReLimiter throws away on the next load. zero_label is how
    // a host knows to render 0 as a named mode and the range above it.
    ReLimiterSettingInfo tf {};
    tf.struct_size = sizeof(tf);
    bool foundTargetFps = false;
    for (uint32_t i = 0; i < api->setting_count(); ++i) {
        ReLimiterSettingInfo info {};
        info.struct_size = sizeof(info);
        api->describe_setting(i, &info);
        if (std::strcmp(info.key, "target_fps") == 0) { tf = info; foundTargetFps = true; }
        // A range must not be inverted, whatever else is true of it.
        CHECK(info.min_value <= info.max_value, "min_value <= max_value");
        // No structural invariant is asserted between min_value and zero_label, because there isn't
        // one: osd_scale is 0.5..2.0 because 0 is simply INVALID there, while target_fps is 30..1000
        // because 0 is special and legal. Only the data knows which, so the behaviour is what gets
        // tested -- below, a labelled zero must survive the setter and an unlabelled one must clamp.
        if (info.zero_label) CHECK(*info.zero_label != '\0', "a zero_label is never empty");
    }
    CHECK(foundTargetFps, "target_fps is in the registry");
    CHECK(tf.zero_label != nullptr, "target_fps has a special zero");
    CHECK(tf.min_value == 30 && tf.max_value == 1000, "target_fps range matches ValidateConfig (30..1000)");

    // 0 must survive the setter untouched. Clamping it up to the minimum would silently convert
    // "automatic" into a hard 30 fps cap -- the exact bug this concept exists to prevent.
    CHECK(api->set_number("target_fps", 0) == 1 && g_config.target_fps == 0, "a special zero is not clamped away");
    // Above zero it clamps normally, in both directions.
    CHECK(api->set_number("target_fps", 141) == 1 && g_config.target_fps == 141, "an in-range target is kept");
    CHECK(api->set_number("target_fps", 7) == 1 && g_config.target_fps == 30, "below the range clamps up, not to 0");
    CHECK(api->set_number("target_fps", 99999) == 1 && g_config.target_fps == 1000, "above the range clamps down");

    // A setting with no special zero still clamps zero normally -- 0 is invalid for osd_scale, not a mode.
    CHECK(api->set_number("osd_scale", 0) == 1 && g_config.osd_scale == 0.5f, "an ordinary zero still clamps");

    // Every labelled zero survives being set, across the whole registry. This is the contract a host's
    // slider depends on: offer the named mode at 0, and setting it actually leaves 0 there.
    for (uint32_t i = 0; i < api->setting_count(); ++i) {
        ReLimiterSettingInfo info {};
        info.struct_size = sizeof(info);
        api->describe_setting(i, &info);
        if (!info.zero_label) continue;
        if (info.type == RELIMITER_TYPE_STRING || info.type == RELIMITER_TYPE_ENUM
            || info.type == RELIMITER_TYPE_KEYBIND) continue;
        double back = -1;
        CHECK(api->set_number(info.key, 0) == 1, "a labelled zero can be set");
        CHECK(api->get_number(info.key, &back) == 1 && back == 0.0, "a labelled zero reads back as zero");
    }

    // Lifecycle reaches ReLimiter's own functions.
    api->apply(); api->save();
    CHECK(g_applied == 1 && g_saved == 1, "apply and save are wired");

    CHECK(api->product_version() && *api->product_version(), "a version is reported");

    std::printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed (%d settings)\n",
                failures ? failures : (int) api->setting_count());
    return failures ? 1 : 0;
}
