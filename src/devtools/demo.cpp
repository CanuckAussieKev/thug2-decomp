// Demos: a run recorded, saved and played back (devtools/demo.h). NOT ORIGINAL.

#include "devtools/demo.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "base/core/checked_index.h"
#include "base/core/down_cast.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/core/parse_number.h"
#include "base/maths/random.h"
#include "build_info.h"
#include "devtools/automation_internal.h"
#include "devtools/screenshot.h"
#include "engine/ext/extension.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/screen.h"
#include "engine/objects/entity.h"
#include "engine/pad_input/input_source.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "engine/sys/mlp.h"
#include "formats/demo_format.h"
#include "game/front_end/front_end.h"
#include "game/front_end/start_level.h"
#include "game/rider/skater_state.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "platform/crt.h"
#include "platform/platform_config.h"
#include "platform/process_flags.h"
#include "platform/run_log.h"
#include "platform/screenshot_files.h"
#include "platform/sdl_keys.h"

namespace devtools::demo {

namespace {

using demo_format::CheckPoint;
using demo_format::Demo;

constexpr uint32_t kSkater = 0x5B8AB877;         // Skater (the local skater's alias)
constexpr uint32_t kStateModuleKey = 0x43686585; // SkaterState
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;   // load_mainmenu (the skate module's level in the front end)
constexpr int kMenuSettleFrames = 60;            // -demo: the main menu up this long before the level loads
constexpr float kTolerance = 0.01F;              // a check point differs beyond this (game units / unit vectors)
constexpr std::size_t kProfileBytes = 16384;     // room for the skater profile's copies
constexpr std::size_t kSettingsBytes = 32768;    // room for an ini section
constexpr std::size_t kKeyTextBytes = 64;
constexpr std::size_t kLoadPrefix = 5;           // "Load_"
constexpr int kMaxEvery = 600;                   // -demo-every: at most a picture every 10 s

enum class Phase : uint8_t { kOff, kRecordArmed, kRecording, kPlayArmed, kPlaying, kFinishing };

struct State {
    Phase phase = Phase::kOff;
    std::string name; // the demo's name: its file's without .demo
    Demo demo;        // the one recorded, or played
    bool counting = false;
    uint32_t frame = 0; // frames begun since the level's load (the frame now: frame - 1)
    bool up = false;    // the level is up (the check track runs)
    // playback
    bool from_command_line = false; // -demo: waiting for the main menu
    int menu_frames = 0;
    std::string capture; // -demo-capture's folder ("": none)
    bool quit_at_end = false;
    int every = 1; // -demo-every: a picture every this many frames
    bool quit_sent = false;
    demo_format::Drift drift;
    std::string track;
    bool random_matched = true;
    bool up_matched = true;
    // the camera drawn this frame (viewport 0)
    std::array<float, 3> camera{};
    std::array<float, 3> forward{};
    bool camera_seen = false;
    // the record key
    int key = 0;
    bool key_up = true;
    modules::FrontEnd* front_end = nullptr;
};

State& state()
{
    static core::NoDestroy<State> value;
    return value.get();
}

bool running() { return state().phase == Phase::kRecording || state().phase == Phase::kPlaying; }

std::string demos_folder() { return game_folder_path("thug2-decomp-demos") + "\\"; }

// A demo's name from what was typed: no folder, no .demo.
std::string clean_name(std::string name)
{
    const size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos) {
        name.erase(0, slash + 1);
    }
    if (name.size() > 5 && _stricmp(name.c_str() + name.size() - 5, ".demo") == 0) {
        name.resize(name.size() - 5);
    }
    return name;
}

// The level's name: its load script's without Load_.
std::string level_name(uint32_t load)
{
    // (the automation channel's names: the game's levels, the ported ones, the names commands used)
    std::string name = frontend::automation::get_name_of(load);
    if (_strnicmp(name.c_str(), "load_", kLoadPrefix) == 0) {
        name.erase(0, kLoadPrefix);
    }
    return name;
}

std::string local_time_text(const char* format)
{
    const std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_s(&local, &now);
    std::array<char, kKeyTextBytes> text{};
    std::strftime(text.data(), text.size(), format, &local);
    return text.data();
}

// An ini section's key=value lines, each key with the section's name before it ("Graphics.msaa").
void add_section(const char* section, std::vector<std::pair<std::string, std::string>>* out)
{
    std::vector<char> buffer(kSettingsBytes);
    const DWORD length = GetPrivateProfileSectionA(section, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                   game_folder_path(build_info::kFileName).c_str());
    size_t at = 0;
    while (at < length) {
        const std::string line(&core::at(buffer, at));
        at += line.size() + 1;
        const size_t equals = line.find('=');
        if (line.empty() || line.front() == ';' || equals == std::string::npos) {
            continue;
        }
        out->emplace_back(std::string(section) + "." + line.substr(0, equals), line.substr(equals + 1));
    }
}

std::vector<std::pair<std::string, std::string>> settings_now()
{
    std::vector<std::pair<std::string, std::string>> settings;
    add_section(build_info::kGraphics, &settings);
    add_section("features", &settings);
    return settings;
}

demo_format::RandomState random_now()
{
    return {.streams = maths::random_streams(), .crt = crt::game_rand_state()};
}

// The skater profile's copies: the network copy (appearance, trick style, specials, info names) and its whole info.
std::vector<uint8_t> profile_bytes(objects::RiderIdentity* profile)
{
    std::vector<uint8_t> bytes(kProfileBytes);
    const int size = profile->emit_into_buffer(bytes.data(), static_cast<int>(bytes.size()), 0);
    bytes.resize(size > 0 ? static_cast<size_t>(size) : 0);
    return bytes;
}
std::vector<uint8_t> details_bytes(objects::RiderIdentity* profile)
{
    std::vector<uint8_t> bytes(kProfileBytes);
    const uint32_t size =
        script::emit_into_buffer(profile->details_params(), bytes.data(), static_cast<uint32_t>(bytes.size()), false, nullptr);
    bytes.resize(size);
    return bytes;
}

objects::RiderIdentity* current_profile()
{
    objects::ProfileRoster* roster = objects::profile_roster();
    return roster != nullptr ? roster->fetch_active_account() : nullptr;
}

// The demo's skater made the current one again: its template if another is current, then its profile and info
// where they differ.
void restore_profile(const Demo& demo)
{
    objects::ProfileRoster* roster = objects::profile_roster();
    objects::RiderIdentity* profile = current_profile();
    if (roster == nullptr || profile == nullptr) {
        return;
    }
    if (demo.skater_name != 0 && profile->fetch_title() != demo.skater_name &&
        roster->get_template(demo.skater_name) != nullptr) {
        roster->set_current_skater(demo.skater_name);
        profile = current_profile();
        get_log("demo: the skater %s made current again", demo.skater.c_str());
    }
    if (!demo.profile.empty() && profile_bytes(profile) != demo.profile) {
        std::vector<uint8_t> copy = demo.profile;
        profile->decode_from_bytes(copy.data(), copy.size());
        get_log("demo: the skater's appearance and tricks put back as recorded");
    }
    if (!demo.details.empty() && details_bytes(profile) != demo.details) {
        std::vector<uint8_t> copy = demo.details;
        const script::StructPtr read = script::make_struct();
        script::decode_from_bytes(read.get(), copy.data(), copy.size());
        profile->details_params()->append_structure_if_any(read.get());
        get_log("demo: the skater's info (stats...) put back as recorded");
    }
}

modules::Skate* skate() { return modules::Skate::sole(); }

// The level `load` up: loaded, the skater there, no loading screen.
bool level_up(uint32_t load)
{
    return render::current_level() != 0 && skate() != nullptr && skate()->level() == load &&
           objects::find_object(kSkater) != nullptr && !frontend::automation::internal::loading_screen_up();
}

bool in_level()
{
    return skate() != nullptr && skate()->level() != 0 && skate()->level() != kLoadMainMenu &&
           objects::find_object(kSkater) != nullptr;
}

CheckPoint point_now(uint32_t frame)
{
    CheckPoint point;
    point.frame = frame;
    if (state().front_end != nullptr && state().front_end->is_halted()) {
        point.flags |= demo_format::kPaused;
    }
    if (auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater))) {
        point.flags |= demo_format::kHasSkater;
        const auto vec = [](const maths::Vec4& v) { return std::array<float, 3>{v.x, v.y, v.z}; };
        point.position = vec(skater->at_point());
        point.velocity = vec(skater->vel());
        point.at = vec(core::at(skater->basis_frame().row, 2));
        point.up = vec(core::at(skater->basis_frame().row, 1));
        auto* pose = core::down_cast<objects::RiderPoseState>(skater->fetch_module(kStateModuleKey));
        point.state = pose != nullptr ? pose->cur_phase() : -1;
    }
    if (state().camera_seen) {
        point.flags |= demo_format::kHasCamera;
        point.camera = state().camera;
        point.camera_forward = state().forward;
    }
    return point;
}

std::string json_text(const std::string& value)
{
    std::string out = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += text::format("\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
        } else {
            out += c;
        }
    }
    return out + "\"";
}

std::string settings_json(const std::vector<std::pair<std::string, std::string>>& settings)
{
    std::string out = "{";
    for (size_t i = 0; i < settings.size(); ++i) {
        out += (i == 0 ? "\n    " : ",\n    ") + json_text(core::at(settings, i).first) + ": " +
               json_text(core::at(settings, i).second);
    }
    return out + (settings.empty() ? "}" : "\n  }");
}

bool write_bytes(const std::string& path, std::span<const uint8_t> bytes)
{
    const std::string text(bytes.begin(), bytes.end());
    const fs::Written written = fs::write_text(path, text);
    return written.written && written.closed;
}

void notice(const std::string& text)
{
    sys::screenshot::show_notice(text);
}

// --- recording ---

void begin_recording(uint32_t load)
{
    State& s = state();
    s.demo = Demo{};
    Demo& demo = s.demo;
    demo.level_load = load;
    demo.level = level_name(load);
    demo.created = local_time_text("%Y-%m-%d %H:%M:%S");
    demo.build = THUG2_BUILD_COMMIT;
    demo.seed = static_cast<uint32_t>(std::time(nullptr)) ^ frame_loop::frames_run();
    demo.crt_seed = demo.seed;
    demo.ports = 1;
    demo.check_stride = 1;
    demo.settings = settings_now();
    if (objects::RiderIdentity* profile = current_profile()) {
        demo.skater_name = profile->fetch_title();
        const char* shown = profile->fetch_shown_name();
        demo.skater = shown != nullptr ? shown : "";
        demo.profile = profile_bytes(profile);
        demo.details = details_bytes(profile);
    }
    if (s.name.empty()) {
        s.name = "demo_" + demo.level + "_" + local_time_text("%Y-%m-%d_%H-%M-%S");
    }
    maths::seed_random(demo.seed);
    crt::game_srand(demo.crt_seed);
    s.counting = true;
    s.frame = 0;
    s.up = false;
    s.phase = Phase::kRecording;
    get_log("demo: recording %s in %s (seed %08X)", s.name.c_str(), demo.level.c_str(), demo.seed);
    notice("Recording");
}

std::string save_recording()
{
    State& s = state();
    s.phase = Phase::kOff;
    s.counting = false;
    const Demo& demo = s.demo;
    if (demo.frames() == 0) {
        return "nothing recorded";
    }
    const std::string folder = demos_folder();
    fs::disk().make_folders(folder.c_str());
    const std::string path = folder + s.name + ".demo";
    if (!write_bytes(path, write_demo(demo))) {
        get_log("demo: couldn't write %s", path.c_str());
        notice("Demo not saved");
        return "couldn't write " + path;
    }
    const std::string json = text::format(
        "{\n  \"name\": %s,\n  \"level\": %s,\n  \"skater\": %s,\n  \"created\": %s,\n  \"build\": %s,\n"
        "  \"frames\": %u,\n  \"seconds\": %.2f,\n  \"up_frame\": %u,\n  \"checks\": %u,\n  \"settings\": %s\n}\n",
        json_text(s.name).c_str(), json_text(demo.level).c_str(), json_text(demo.skater).c_str(),
        json_text(demo.created).c_str(), json_text(demo.build).c_str(), demo.frames(),
        static_cast<double>(demo.frames()) / 60.0, demo.up_frame, static_cast<unsigned>(demo.checks.size()),
        settings_json(demo.settings).c_str());
    fs::write_text(folder + s.name + ".json", json);
    get_log("demo: saved %s - %u frames (%.1f s), the level up at frame %u, %u check points", path.c_str(),
            demo.frames(), static_cast<double>(demo.frames()) / 60.0, demo.up_frame,
            static_cast<unsigned>(demo.checks.size()));
    notice("Demo saved: " + s.name);
    return "saved " + path;
}

// --- playback ---

bool load_demo(const std::string& name, std::string* why)
{
    State& s = state();
    const std::string path = demos_folder() + clean_name(name) + ".demo";
    std::string bytes;
    if (!fs::read_text(path, &bytes)) {
        *why = "can't read " + path;
        return false;
    }
    Demo demo;
    const std::vector<uint8_t> data(bytes.begin(), bytes.end());
    if (!demo_format::read_demo(data, &demo, why)) {
        *why = path + ": " + *why;
        return false;
    }
    s.demo = std::move(demo);
    s.name = clean_name(name);
    return true;
}

// Where the playback's track and summary go: the capture's folder, or the demos' folder.
std::string output_base()
{
    const State& s = state();
    if (!s.capture.empty()) {
        return s.capture + "\\";
    }
    return demos_folder() + s.name + ".play.";
}

std::string start_playback()
{
    State& s = state();
    const std::string online = frontend::automation::internal::net_refusal();
    if (!online.empty()) {
        s.phase = Phase::kOff;
        return "refused: " + online;
    }
    restore_profile(s.demo);
    s.phase = Phase::kPlayArmed;
    std::string why;
    if (!frontend::begin_level::read_in_load(s.demo.level_load, false, &why)) {
        s.phase = Phase::kOff;
        get_log("demo: %s can't be played - %s", s.name.c_str(), why.c_str());
        return why;
    }
    return {};
}

void begin_playback(uint32_t load)
{
    State& s = state();
    if (load != s.demo.level_load) {
        get_log("demo: another level loaded (%s, not %s) - the playback stops", level_name(load).c_str(),
                s.demo.level.c_str());
        s.phase = Phase::kOff;
        return;
    }
    maths::seed_random(s.demo.seed);
    crt::game_srand(s.demo.crt_seed);
    s.counting = true;
    s.frame = 0;
    s.up = false;
    s.drift = {};
    s.track = std::string(demo_format::kCheckHeader) + "\n";
    s.random_matched = true;
    s.up_matched = true;
    s.phase = Phase::kPlaying;
    get_log("demo: playing %s in %s - %u frames (%.1f s), skater %s, recorded %s", s.name.c_str(), s.demo.level.c_str(),
            s.demo.frames(), static_cast<double>(s.demo.frames()) / 60.0, s.demo.skater.c_str(),
            s.demo.created.c_str());
}

void finish_playback(bool ended)
{
    State& s = state();
    s.counting = false;
    const demo_format::Drift& drift = s.drift;
    std::string verdict;
    if (drift.compared == 0) {
        verdict = "no check points compared";
    } else if (drift.first_frame < 0) {
        verdict = text::format("identical to the recording (%u check points)", drift.compared);
    } else {
        verdict = text::format("first differs at frame %lld (skater %.3f, camera %.3f apart%s); largest drift skater "
                               "%.3f, camera %.3f",
                               static_cast<long long>(drift.first_frame), static_cast<double>(drift.first.skater),
                               static_cast<double>(drift.first.camera), drift.first.other ? ", state / rows differ" : "",
                               static_cast<double>(drift.max_skater), static_cast<double>(drift.max_camera));
    }
    get_log("demo: %s %s - drift: %s%s", s.name.c_str(), ended ? "played to its end" : "stopped", verdict.c_str(),
            s.random_matched ? "" : " (the random numbers differed when the level was up: put back)");
    const std::string base = output_base();
    fs::write_text(base + "track.txt", s.track);
    const std::string json = text::format(
        "{\n  \"demo\": %s,\n  \"level\": %s,\n  \"frames\": %u,\n  \"up_frame\": %u,\n  \"ended\": %s,\n"
        "  \"compared\": %u,\n  \"first_divergence_frame\": %lld,\n  \"first_skater_distance\": %.4f,\n"
        "  \"first_camera_distance\": %.4f,\n  \"max_skater_distance\": %.4f,\n  \"max_camera_distance\": %.4f,\n"
        "  \"random_matched_at_up\": %s,\n  \"up_matched\": %s,\n  \"capture\": %s,\n  \"settings\": %s,\n"
        "  \"recorded_settings\": %s\n}\n",
        json_text(s.name).c_str(), json_text(s.demo.level).c_str(), s.demo.frames(), s.demo.up_frame,
        ended ? "true" : "false", drift.compared, static_cast<long long>(drift.first_frame),
        static_cast<double>(drift.first.skater), static_cast<double>(drift.first.camera),
        static_cast<double>(drift.max_skater), static_cast<double>(drift.max_camera),
        s.random_matched ? "true" : "false", s.up_matched ? "true" : "false", json_text(s.capture).c_str(),
        settings_json(settings_now()).c_str(), settings_json(s.demo.settings).c_str());
    fs::write_text(base + "run.json", json);
    get_log("demo: the run's track and summary in %strack.txt / run.json", base.c_str());
    notice(ended ? "Demo over" : "Demo stopped");
    s.phase = (s.quit_at_end || !s.capture.empty()) ? Phase::kFinishing : Phase::kOff;
}

// --- the frame ---

void on_level_load(uint32_t load)
{
    State& s = state();
    if (s.phase == Phase::kRecordArmed) {
        begin_recording(load);
    } else if (s.phase == Phase::kPlayArmed) {
        begin_playback(load);
    } else if (running()) {
        get_log("demo: another level started (%s) during the %s", level_name(load).c_str(),
                s.phase == Phase::kRecording ? "recording - it goes on" : "playback");
    }
}

void frame_begun()
{
    State& s = state();
    if (!s.counting || !running()) {
        return;
    }
    ++s.frame;
    if (s.phase == Phase::kRecording) {
        // (the port's data as the last frame had it, until the poll writes this frame's)
        const demo_format::PortData last = s.demo.inputs.empty() ? demo_format::PortData{} : s.demo.inputs.back();
        s.demo.inputs.push_back(last);
    }
}

void port_polled(input::PortPoll& poll)
{
    State& s = state();
    if (poll.port != 0 || !s.counting || s.frame == 0) {
        return;
    }
    const uint32_t now = s.frame - 1;
    const size_t bytes = std::min(poll.data.size(), demo_format::kPortBytes);
    if (s.phase == Phase::kRecording && now < s.demo.inputs.size()) {
        std::memcpy(core::at(s.demo.inputs, now).data(), poll.data.data(), bytes);
    } else if (s.phase == Phase::kPlaying) {
        if (const demo_format::PortData* data = s.demo.input(now, 0)) {
            std::memcpy(poll.data.data(), data->data(), bytes);
            poll.connected = true;
        }
    }
}

void start_capture()
{
    State& s = state();
    const auto every = static_cast<uint32_t>(s.every);
    const uint32_t frames = s.demo.frames() - s.demo.up_frame;
    const uint32_t pictures = (frames + every - 1) / every;
    const std::string result = sys::screenshot::start_series(static_cast<int>(pictures), s.capture,
                                                             {.long_series = true, .every = s.every});
    if (!result.empty()) {
        get_log("demo: the capture didn't start - %s", result.c_str());
    } else {
        get_log("demo: capturing %u frames (one in %u from frame %u) into %s", pictures, every, s.demo.up_frame,
                s.capture.c_str());
    }
}

void frame_ending(bool rendering)
{
    State& s = state();
    if (s.phase == Phase::kFinishing) {
        if (!s.quit_sent && !sys::screenshot::series_running()) {
            s.quit_sent = true;
            get_log("demo: done - quitting (%s)", sys::screenshot::series_result().c_str());
            SDL_Event event = {};
            event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&event);
        }
        return;
    }
    if (!s.counting || s.frame == 0 || !running()) {
        s.camera_seen = false;
        return;
    }
    const uint32_t now = s.frame - 1;
    if (s.phase == Phase::kRecording) {
        if (!s.up && level_up(s.demo.level_load)) {
            s.up = true;
            s.demo.up_frame = now;
            s.demo.random_at_up = random_now();
            get_log("demo: %s up at frame %u - the check track starts", s.demo.level.c_str(), now);
        }
        if (s.up && (now - s.demo.up_frame) % s.demo.check_stride == 0) {
            s.demo.checks.push_back(point_now(now));
        }
    } else {
        if (!s.up && now == s.demo.up_frame) {
            s.up = true;
            s.up_matched = level_up(s.demo.level_load);
            if (!s.up_matched) {
                get_log("demo: the level isn't up yet at frame %u, where the recording's was", now);
            }
            if (random_now() != s.demo.random_at_up) {
                s.random_matched = false;
                maths::set_random_streams(s.demo.random_at_up.streams);
                crt::game_srand(s.demo.random_at_up.crt);
                get_log("demo: the random numbers differ from the recording's at frame %u - put back", now);
            }
            if (!s.capture.empty()) {
                start_capture();
            }
        }
        if (s.up && (now - s.demo.up_frame) % s.demo.check_stride == 0) {
            const CheckPoint point = point_now(now);
            s.track += demo_format::check_line(point) + "\n";
            if (const CheckPoint* recorded = s.demo.check_at(now)) {
                const bool was_same = s.drift.first_frame < 0;
                demo_format::add_to_drift(&s.drift, *recorded, point, kTolerance);
                if (was_same && s.drift.first_frame >= 0) {
                    get_log("demo: frame %u differs from the recording - skater %.3f, camera %.3f apart%s", now,
                            static_cast<double>(s.drift.first.skater), static_cast<double>(s.drift.first.camera),
                            s.drift.first.other ? ", state / rows differ" : "");
                }
            }
        }
        if (s.up && !rendering && !s.capture.empty()) {
            get_log("demo: frame %u wasn't drawn - the capture is a frame behind from here", now);
        }
        if (now + 1 >= s.demo.frames()) {
            finish_playback(true);
        }
    }
    s.camera_seen = false;
}

void check_key(modules::FrontEnd* front_end)
{
    State& s = state();
    const input::KeyState keys = input::keys();
    const bool down = s.key != 0 && !keys.empty() && static_cast<size_t>(s.key) < keys.size() &&
                      core::at(keys, static_cast<size_t>(s.key)) != 0U;
    const bool pressed = down && s.key_up;
    s.key_up = !down;
    if (!pressed || frame_loop::frames_run() == 0) {
        return;
    }
    if (s.phase == Phase::kRecording) {
        stop_now();
    } else if (s.phase == Phase::kPlaying) {
        stop_now();
    } else if (s.phase == Phase::kOff && !(front_end != nullptr && front_end->is_halted())) {
        const std::string result = record_now("");
        if (!result.empty()) {
            get_log("demo: F6 - %s", result.c_str());
        }
    }
}

void menu_tick(modules::FrontEnd* front_end, bool game_owned)
{
    State& s = state();
    s.front_end = front_end;
    if (!game_owned) {
        check_key(front_end);
    }
    // -demo: once the main menu has been up a moment (its scripts loaded), as -level starts its level
    if (s.phase == Phase::kPlayArmed && s.from_command_line) {
        const bool menu = render::current_level() == 0 && !(front_end != nullptr && front_end->is_halted()) &&
                          script::resolve_symbol(checksums::hash_from_text("main_menu_skate_now")) != nullptr;
        s.menu_frames = menu ? s.menu_frames + 1 : 0;
        if (s.menu_frames >= kMenuSettleFrames) {
            s.from_command_line = false;
            const std::string failed = start_playback();
            if (!failed.empty()) {
                get_log("demo: %s", failed.c_str());
            }
        }
    }
}

// The command line and the key, once.
void prepare_all()
{
    State& s = state();
    frontend::begin_level::set_load_listener(on_level_load);
    std::array<char, kKeyTextBytes> setting = {""};
    build_info::fetch_text(build_info::kKeybinds, "DemoRecord", "", setting.data(), setting.size());
    auto by_name = [](const std::string& text) { return platform::scancode_from_name(text); };
    auto by_number = [](int code) { return platform::scancode_from_sdl2(code); };
    s.key = sys::screenshot::parse_key(setting.data(), SDL_SCANCODE_F6, by_name, by_number);

    const std::string& line = sys::startup_command_line();
    const std::string play = frontend::begin_level::option_value(line, "demo");
    const std::string record = frontend::begin_level::option_value(line, "record");
    if (!play.empty()) {
        std::string why;
        if (!load_demo(play, &why)) {
            get_log("demo: -demo %s - %s", play.c_str(), why.c_str());
            return;
        }
        std::string capture = frontend::begin_level::option_value(line, "demo-capture");
        const bool absolute = capture.size() >= 2 && (capture[1] == ':' || (capture[0] == '\\' && capture[1] == '\\'));
        if (!capture.empty() && !absolute) {
            capture = game_folder_path(capture.c_str());
        }
        while (!capture.empty() && (capture.back() == '\\' || capture.back() == '/')) {
            capture.pop_back();
        }
        s.capture = capture;
        s.quit_at_end = line.find("-demo-quit") != std::string::npos;
        const std::string every = frontend::begin_level::option_value(line, "demo-every");
        s.every = every.empty() ? 1 : std::clamp(text::to_int(every.c_str()), 1, kMaxEvery);
        s.from_command_line = true;
        s.phase = Phase::kPlayArmed;
        if (!s.capture.empty()) {
            fs::disk().make_folders((s.capture + "\\").c_str());
        }
        get_log("demo: -demo %s - played once the main menu is up%s%s (one frame in %d)", s.name.c_str(),
                s.capture.empty() ? "" : ", captured into ", s.capture.c_str(), s.every);
    } else if (!record.empty()) {
        s.name = clean_name(record);
        s.phase = Phase::kRecordArmed;
        get_log("demo: -record %s - recording from the next level load%s", s.name.c_str(),
                *frontend::begin_level::get_requested() == '\0' ? " (no -level given: load one, or press F6 in one)" : "");
    }
    get_log("demo: %s records a demo in a level (again to stop)", SDL_GetScancodeName(static_cast<SDL_Scancode>(s.key)));
}

class DemoExtension final : public ext::Extension {
public:
    void prepare_input() override { prepare_all(); }
    void menu_frame(modules::FrontEnd* front_end, bool game_owned) override { menu_tick(front_end, game_owned); }
    void view_drawn(const ext::ViewNote& view) override
    {
        if (view.viewport == 0) {
            state().camera = view.position;
            state().forward = view.forward;
            state().camera_seen = true;
        }
    }
    void before_present(bool rendering) override { frame_ending(rendering); }
    bool keeps_every_frame() override { return running(); }
    bool draws_unfocused() override { return running() || state().phase == Phase::kPlayArmed; }
    bool holds_quit() override
    {
        if (state().phase == Phase::kRecording) {
            stop_now();
        }
        return false;
    }
};

class DemoSource final : public input::Source {
public:
    void read_frame() override { frame_begun(); }
    void poll_port(input::PortPoll& poll) override { port_polled(poll); }
};

} // namespace

ext::Extension& extension()
{
    static DemoExtension demo;
    return demo;
}

input::Source& input_source()
{
    static DemoSource source;
    return source;
}

std::string record_now(const std::string& name)
{
    State& s = state();
    if (s.phase != Phase::kOff) {
        return "a demo is running";
    }
    const std::string online = frontend::automation::internal::net_refusal();
    if (!online.empty()) {
        return "refused: " + online;
    }
    if (!in_level()) {
        return "not in a level";
    }
    s.name = clean_name(name);
    s.phase = Phase::kRecordArmed;
    std::string why;
    if (!frontend::begin_level::read_in_load(skate()->level(), false, &why)) {
        s.phase = Phase::kOff;
        return why;
    }
    return "recording " + s.name;
}

std::string stop_now()
{
    State& s = state();
    switch (s.phase) {
    case Phase::kRecording:
        return save_recording();
    case Phase::kPlaying:
        finish_playback(false);
        if (s.phase == Phase::kFinishing) {
            s.phase = Phase::kOff;
        }
        return "stopped";
    case Phase::kRecordArmed:
    case Phase::kPlayArmed:
        s.phase = Phase::kOff;
        return "stopped";
    default:
        return "no demo running";
    }
}

std::string play_now(const std::string& name)
{
    State& s = state();
    if (s.phase != Phase::kOff) {
        return "a demo is running";
    }
    std::string why;
    if (!load_demo(name, &why)) {
        return why;
    }
    s.capture.clear();
    s.quit_at_end = false;
    const std::string failed = start_playback();
    return failed.empty() ? "playing " + s.name : failed;
}

std::string status_text()
{
    const State& s = state();
    switch (s.phase) {
    case Phase::kRecording:
        return text::format("recording %s: frame %u", s.name.c_str(), s.frame);
    case Phase::kPlaying:
        return text::format("playing %s: frame %u of %u", s.name.c_str(), s.frame, s.demo.frames());
    case Phase::kRecordArmed:
        return "recording " + s.name + " from the next level load";
    case Phase::kPlayArmed:
        return "playing " + s.name + " once its level loads";
    case Phase::kFinishing:
        return "finishing " + s.name;
    default:
        return "no demo running";
    }
}

} // namespace devtools::demo
