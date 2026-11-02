// NOT ORIGINAL - the screenshot key (platform/screenshot.h).

#include "platform/screenshot.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <SDL2/SDL.h>
#include <array>

#include "build_info.h"
#include "engine/core/checksums.h"
#include "frontend/automation.h"
#include "frontend/front_end.h"
#include "editor/level_editor.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_scenery.h"
#include "engine/gfx/screenshot_capture.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "platform/platform_config.h"
#include "engine/sys/mlp.h"
#include "platform/stopwatch.h"
#include "platform/screenshot_files.h"
#include "platform/sdl_events.h"
#include "platform/sdl_window.h"
#include "platform/run_log.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "engine/gfx/device.h"
#include "engine/gfx/engine_glue.h"
#include "engine/util/unique_handle.h"
#include "engine/core/raw_view.h"
#include "platform/code_address.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace sys::screenshot {

namespace {

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu (the skate module's level in the front end)
constexpr int kLogLines = 20;
constexpr uint32_t kMessageMs = 2000;

int g_key;
std::atomic<bool> g_requested{false};
std::string g_event_note; // (request_event_shot's: the next picture is an event's)

// The camera of viewport 0 as last drawn.
struct View {
    std::array<float, 3> pos;
    std::array<float, 3> forward;
    uint32_t frame;
    bool noted;
} g_view;

// The writer: one picture at a time, on its own thread (made RGB, PNG-encoded, written with its sidecar). The main
// thread reads `done` and then the rest.
struct Job {
    TickCycle frame;
    std::string folder, png, txt, sidecar;
    std::atomic<bool> done{false};
    bool ok = false;
    std::string error;
    double milliseconds = 0.0;
    int mouse_x_ = -1, mouse_y_ = -1; // (in the image's pixels; -1: not over the window)
    std::string note;                 // (an event's picture: request_event_shot)
};
std::shared_ptr<Job> g_job;

// The message for the player, held while a loading screen is up (for up to 10 s).
std::string g_message;
uint32_t g_message_since;
float g_message_y = 40.0f; // (show_notice's lower, clear of the score: 100)

// --- the game's state ---

modules::Skate* skate_module() { return modules::g_skate_module.get(); }
// The level loaded: its load script's checksum (the skate module's +0x378; load_mainmenu in the front end).
uint32_t current_load_state() { return skate_module() ? modules::g_skate_module->level_ : 0; }
bool loading_screen_up()
{
    return render::g_loading_screen_shown != 0 || render::g_loading_bar_timer != 0 ||
           static_cast<uint8_t>(render::g_loading_screen_up) != 0;
}
modules::FrontEnd* the_front_end() { return modules::g_front_end_instance.get(); }
std::string get_name_of(uint32_t checksum)
{
    if (const char* name = script::look_up_hash_name(checksum))
        return name;
    std::array<char, 16> text{};
    std::snprintf(text.data(), sizeof(text), "0x%08X", checksum);
    return text.data();
}
std::string active_ruleset_label()
{
    goals::MatchRules* mode = goals::g_game_mode.get();
    return mode ? get_name_of(mode->get_name_key()) : "";
}
// The local skater (the skate module's 0x00566520; null with none).
objects::GameEntity* local_skater()
{
    return skate_module() ? skate_module()->get_home_rider() : nullptr;
}

std::string vec(std::span<const float, 3> v, const char* format = "(%.1f, %.1f, %.1f)")
{
    std::array<char, 96> text{};
    std::snprintf(text.data(), sizeof(text), format, core::at(v, 0), core::at(v, 1), core::at(v, 2));
    return text.data();
}

// The file name at the end of a path.
std::string_view file_name_of(const char* path)
{
    const std::string_view full(path);
    const size_t slash = full.rfind('\\');
    return slash == std::string_view::npos ? full : full.substr(slash + 1);
}

std::string file_time(const char* path)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data))
        return {};
    FILETIME local;
    SYSTEMTIME t;
    if (!FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &t))
        return {};
    std::array<char, 64> text{};
    std::snprintf(text.data(), sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u, %llu bytes", t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond,
                  (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
    return text.data();
}

// Everything the sidecar says, read on the main thread (every field safe when it isn't there).
std::vector<Section> gather_sections(const TickCycle& frame, const std::string& png, const std::string& folder,
                                     const std::string& taken)
{
    std::vector<Section> sections;
    std::array<char, 256> text{};

    Section shot{"screenshot", {}};
    shot.fields.push_back({"file", png});
    shot.fields.push_back({"folder", folder});
    shot.fields.push_back({"taken", taken});
    std::snprintf(text.data(), sizeof(text), "%d x %d (%s)", frame.width, frame.height, format_name(frame.format));
    shot.fields.push_back({"image", text.data()});
    int w = 0, h = 0;
    if (SDL_Window* window = sys::sdl_window())
        SDL_GetWindowSize(window, &w, &h);
    if (w > 0 && h > 0) {
        std::snprintf(text.data(), sizeof(text), "%d x %d", w, h);
        shot.fields.push_back({"window", text.data()});
    } else {
        shot.fields.push_back({"window", ""});
    }
    sections.push_back(shot);

    Section build{"build", {}};
    std::array<char, MAX_PATH> exe = {""};
    GetModuleFileNameA(nullptr, exe.data(), MAX_PATH);
    build.fields.push_back({"exe", std::string(file_name_of(exe.data()))});
    build.fields.push_back({"exe file", file_time(exe.data())});
    HMODULE dll = nullptr;
    std::array<char, MAX_PATH> dll_path = {""};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(code_address(&gather_sections)), &dll))
        GetModuleFileNameA(dll, dll_path.data(), MAX_PATH);
    build.fields.push_back(
        {"dll",
         core::at(dll_path, 0) ? std::string(file_name_of(dll_path.data())) : ""});
    build.fields.push_back({"dll file", core::at(dll_path, 0) ? file_time(dll_path.data()) : ""});
    build.fields.push_back({"dll commit", THUG2_BUILD_COMMIT});
    sections.push_back(build);

    Section game{"game", {}};
    const uint32_t load = current_load_state();
    const bool in_level = load && load != kLoadMainMenu;
    const bool loading = loading_screen_up();
    game.fields.push_back({"where", !load ? "starting up" : in_level ? "level" : "front end"});
    game.fields.push_back({"level load script", load ? get_name_of(load) : ""});
    const uint32_t scene =
        frontend::level_editor::scenery::world_scene_key(frontend::level_editor::scenery::main_scene());
    game.fields.push_back({"scene", scene ? get_name_of(scene) : ""});
    game.fields.push_back({"game mode", active_ruleset_label()});
    const modules::FrontEnd* front_end = the_front_end();
    game.fields.push_back({"paused", front_end ? (front_end->is_halted_ ? "yes" : "no") : ""});
    game.fields.push_back({"loading screen", loading ? "yes" : "no"});
    game.fields.push_back({"level editor", frontend::level_editor::live_now() ? "open" : "closed"});
    game.fields.push_back({"frames run", std::to_string(frame_loop::frames_run())});
    sections.push_back(game);

    Section skater{"skater", {}};
    if (in_level && !loading) {
        if (const objects::GameEntity* s = local_skater()) {
            std::array<float, 3> pos = {{s->at_point_.x, s->at_point_.y, s->at_point_.z}};
            skater.fields.push_back({"position", vec(pos)});
            const maths::Vec4& at = core::at(s->basis_frame_.row, 2);
            const double heading =
                std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)) * 180.0 / 3.14159265358979;
            std::snprintf(text.data(), sizeof(text), "%.1f degrees (0 = +z, 90 = +x)", heading);
            skater.fields.push_back({"heading", text.data()});
        }
    }
    sections.push_back(skater);

    Section camera{"camera", {}};
    if (g_view.noted && frame_loop::frames_run() - g_view.frame < 10) {
        camera.fields.push_back({"position", vec(g_view.pos)});
        camera.fields.push_back({"looking along", vec(g_view.forward, "(%.3f, %.3f, %.3f)")});
    }
    sections.push_back(camera);

    if (frontend::level_editor::live_now()) {
        Section editor{"level editor", {}};
        for (auto& [key, value] : frontend::level_editor::describe_for_screenshot())
            editor.fields.push_back({key, value});
        sections.push_back(editor);
    }
    return sections;
}

// --- the message ---

void show_message(const std::string& text)
{
    if (frontend::level_editor::live_now()) {
        frontend::level_editor::hud::get_message(text, kMessageMs / 1000.0);
        return;
    }
    // A panel message (as the restart tester's), once the game's scripts are up.
    static const uint32_t kScript = checksums::hash_from_text("create_panel_message");
    if (!current_load_state() || !script::script_exists(kScript))
        return;
    auto crc = [](const char* name) { return checksums::hash_from_text(name); };
    gsl::owner<script::Struct*> params = script::new_struct();
    params->append_word(crc("text"), text.c_str());
    params->put_name_key(crc("id"), crc("thug2_decomp_screenshot_message"));
    params->insert_pair(crc("pos"), 320.0f, g_message_y);
    params->put_int(crc("time"), static_cast<int>(kMessageMs));
    script::spawn_script(kScript, params, 0, nullptr, 0, 0, false, false, false, false);
    params->get_clear();
    script::delete_struct(params);
}

void tick_message()
{
    if (g_message.empty())
        return;
    if (SDL_GetTicks() - g_message_since > 10000) {
        g_message.clear();
        return;
    }
    if (loading_screen_up())
        return;
    show_message(g_message);
    g_message.clear();
    g_message_y = 40.0f;
}

// --- taking and writing ---

void emit_out(const std::shared_ptr<Job>& job)
{
    LARGE_INTEGER start, end, frequency;
    QueryPerformanceCounter(&start);
    std::vector<uint8_t> rgb, png;
    const bool read = to_rgb(job->frame, &rgb);
    if (read)
        mark_cursor(&rgb, job->frame.width, job->frame.height, job->mouse_x_, job->mouse_y_,
                    std::max(12, job->frame.height / 60));
    if (!read) {
        job->error = "the pixels couldn't be read";
    } else if (!encode_png(rgb.data(), job->frame.width, job->frame.height, &png)) {
        job->error = "the PNG couldn't be made";
    } else {
        auto write_new = [](const std::string& path, const void* data, size_t size) {
            const util::UniqueHandle file(
                CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!file)
                return false;
            DWORD written = 0;
            return WriteFile(file.get(), data, static_cast<DWORD>(size), &written, nullptr) && written == size;
        };
        const std::string png_path = job->folder + job->png;
        const std::string txt_path = job->folder + job->txt;
        if (!write_new(png_path, png.data(), png.size()))
            job->error = "couldn't write " + png_path + " (error " + std::to_string(GetLastError()) + ")";
        else if (!write_new(txt_path, job->sidecar.data(), job->sidecar.size()))
            job->error = "couldn't write " + txt_path + " (error " + std::to_string(GetLastError()) + ")";
        else
            job->ok = true;
    }
    job->frame.bytes.clear();
    job->frame.bytes.shrink_to_fit();
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    job->milliseconds =
        static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
    job->done = true;
}

// The folder (ending in a backslash): [features] screenshot_dir or Screenshots, in the game folder unless absolute.
std::string folder_path()
{
    std::array<char, MAX_PATH> setting = {""};
    build_info::fetch_text("features", "screenshot_dir", "", setting.data(), sizeof(setting));
    std::string folder = setting.data();
    while (!folder.empty() && (folder.back() == ' ' || folder.back() == '\t'))
        folder.pop_back();
    if (folder.empty())
        folder = "Screenshots";
    const bool absolute = folder.size() >= 2 && (core::at(folder, 1) == ':' || (core::at(folder, 0) == '\\' && core::at(folder, 1) == '\\'));
    if (!absolute)
        folder = game_folder_path(folder.c_str());
    for (char& c : folder)
        if (c == '/')
            c = '\\';
    if (folder.back() != '\\')
        folder += '\\';
    return folder;
}

// The folder, made if it isn't there; "" if it can't be.
std::string get_folder()
{
    std::string folder = folder_path();
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(folder), error);
    const DWORD attributes = GetFileAttributesA(folder.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        get_log("screenshot: can't make the folder %s", folder.c_str());
        return {};
    }
    return folder;
}

void get_take()
{
    TickCycle frame;
    std::string error;
    if (!gfx::screenshot_capture::capture_back_buffer(&frame, &error)) {
        get_log("screenshot: not taken - %s", error.c_str());
        g_message = "Screenshot failed: " + error;
        g_message_since = SDL_GetTicks();
        return;
    }
    std::string folder = get_folder();
    if (folder.empty()) {
        g_message = "Screenshot failed: no folder (see the log)";
        g_message_since = SDL_GetTicks();
        return;
    }
    std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_s(&local, &now);
    const Stamp stamp{local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec};
    const std::string png = pick_name(file_stem(stamp), [&folder](const std::string& name) {
        return GetFileAttributesA((folder + name).c_str()) != INVALID_FILE_ATTRIBUTES;
    });
    if (png.empty()) {
        get_log("screenshot: no free name in %s", folder.c_str());
        return;
    }
    std::array<char, 64> taken{};
    std::snprintf(taken.data(), sizeof(taken), "%04d-%02d-%02d %02d:%02d:%02d", stamp.year, stamp.month, stamp.day, stamp.hour,
                  stamp.minute, stamp.second);
    auto job = std::make_shared<Job>();
    // The mouse, in the image's pixels (the window may be scaled to the back buffer): marked on the picture and noted.
    std::string mouse;
    int mx = 0, my = 0, w = 0, h = 0;
    SDL_Window* window = sys::sdl_window();
    if (window && (SDL_GetMouseFocus() == window)) {
        SDL_GetMouseState(&mx, &my);
        SDL_GetWindowSize(window, &w, &h);
    }
    if (w > 0 && h > 0) {
        job->mouse_x_ = static_cast<int>(static_cast<long long>(mx) * frame.width / w);
        job->mouse_y_ = static_cast<int>(static_cast<long long>(my) * frame.height / h);
        std::array<char, 96> at{};
        std::snprintf(at.data(), sizeof(at), "(%d, %d) in the image (marked with a cross)", job->mouse_x_, job->mouse_y_);
        mouse = at.data();
    } else {
        mouse = "not over the window";
    }
    job->folder = folder;
    job->png = png;
    job->txt = sidecar_name(png);
    std::vector<Section> sections = gather_sections(frame, png, folder, taken.data());
    sections.front().fields.push_back({"mouse", mouse});
    if (!g_event_note.empty()) {
        job->note = g_event_note;
        sections.front().fields.push_back({"for", g_event_note});
    }
    job->sidecar = format_sidecar("THUG2 decomp screenshot " + png, sections, get_log_tail(kLogLines));
    job->frame = std::move(frame);
    g_job = job;
    std::thread(emit_out, job).detach();
}

// --- a frame series ---

struct Series {
    std::string folder; // (ending in a backslash)
    int total = 0, taken = 0;
    std::string sidecar; // (made at the first frame, written at the end with the frames' times)
    std::string times;   // a line a frame: its number, the game's frame and vblank count, the game clock (ms)
    std::atomic<int> in_flight_{0}, written{0};
    std::mutex mutex;
    std::string error; // the first failure (under `mutex`)
};
std::shared_ptr<Series> g_series;
std::string& series_result_state()
{
    static std::string value = "none";
    return value;
}

bool write_new_file(const std::string& path, const void* data, size_t size)
{
    const util::UniqueHandle file(
        CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        return false;
    DWORD written = 0;
    return WriteFile(file.get(), data, static_cast<DWORD>(size), &written, nullptr) && written == size;
}

void write_series_frame(const std::shared_ptr<Series>& series, const TickCycle& frame, int number)
{
    std::vector<uint8_t> rgb, png;
    std::string error;
    const std::string path = series->folder + series_frame_name(number);
    if (!to_rgb(frame, &rgb))
        error = "frame " + std::to_string(number) + ": the pixels couldn't be read";
    else if (!encode_png(rgb.data(), frame.width, frame.height, &png))
        error = "frame " + std::to_string(number) + ": the PNG couldn't be made";
    else if (!write_new_file(path, png.data(), png.size()))
        error = "couldn't write " + path + " (error " + std::to_string(GetLastError()) + ")";
    if (error.empty()) {
        ++series->written;
    } else {
        const std::scoped_lock lock(series->mutex);
        if (series->error.empty())
            series->error = error;
    }
    --series->in_flight_;
}

// The series' frame (from before_present, rendering).
void take_series_frame()
{
    const std::shared_ptr<Series> series = g_series;
    TickCycle frame;
    std::string error;
    if (!gfx::screenshot_capture::capture_back_buffer(&frame, &error)) {
        const std::scoped_lock lock(series->mutex);
        if (series->error.empty())
            series->error = "frame " + std::to_string(series->taken + 1) + " not taken - " + error;
        series->taken = series->total; // (no more)
        return;
    }
    if (series->sidecar.empty()) {
        std::time_t now = std::time(nullptr);
        std::tm local = {};
        localtime_s(&local, &now);
        std::array<char, 64> taken{};
        std::snprintf(taken.data(), sizeof(taken), "%04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900, local.tm_mon + 1,
                      local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
        std::vector<Section> sections = gather_sections(frame, series_frame_name(1), series->folder, taken.data());
        sections.front().title = "series";
        sections.front().fields.push_back(
            {"frames", std::to_string(series->total) + " (frame_0001.png on, one a rendered frame)"});
        series->sidecar = format_sidecar("THUG2 decomp frame series", sections, get_log_tail(kLogLines));
    }
    // (at most kSeriesInFlight waiting: the frame waits for one)
    while (series->in_flight_ >= kSeriesInFlight)
        Sleep(1);
    ++series->in_flight_;
    const int number = ++series->taken;
    std::array<char, 96> line{};
    std::snprintf(line.data(), sizeof(line), "%5d  %10u  %10llu  %10u\n", number, frame_loop::frames_run(),
                  static_cast<unsigned long long>(frame_clock::fetch_retrace_count()), frame_clock::get_time());
    series->times += line.data();
    std::thread(write_series_frame, series, std::move(frame), number).detach();
}

// Once a frame: the next frame of a series taken; a finished series' result logged.
void tick_series(bool rendering)
{
    const std::shared_ptr<Series> series = g_series;
    if (!series)
        return;
    if (series->taken < series->total) {
        if (rendering)
            take_series_frame();
        return;
    }
    if (series->in_flight_ > 0)
        return;
    std::string text = series->sidecar + "\n[frames]\nframe  game frame     vblanks  clock (ms)\n" + series->times;
    write_new_file(series->folder + kSeriesSidecar, text.data(), text.size());
    {
        const std::scoped_lock lock(series->mutex);
        if (series->error.empty())
            series_result_state() = "ok: " + std::to_string(series->written.load()) + " frames in " + series->folder;
        else
            series_result_state() =
                "error: " + series->error + " (" + std::to_string(series->written.load()) + " written)";
    }
    get_log("screenshot: series - %s", series_result_state().c_str());
    g_series.reset();
}

void handle_event(SDL_Event* event)
{
    if (event->type == SDL_KEYDOWN && !event->key.repeat && static_cast<int>(event->key.keysym.scancode) == g_key)
        g_requested = true;
}

// Whether Windows' Snipping Tool takes the Print Screen key (its setting; Windows 11 turns it on by default).
bool get_snipping_takes_print_screen()
{
    DWORD value = 0, size = sizeof(value);
    return RegGetValueA(HKEY_CURRENT_USER, "Control Panel\\Keyboard", "PrintScreenKeyForSnippingEnabled",
                        RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
           value != 0;
}

} // namespace

bool is_switched_on()
{
    static const bool kEnabled = config_int("features", "screenshots", 1) != 0;
    return kEnabled;
}

int get_key() { return g_key; }

void prepare_all()
{
    if (!is_switched_on()) {
        get_log("screenshot key: off ([features] screenshots=0)");
        return;
    }
    const bool snipping = get_snipping_takes_print_screen();
    std::array<char, 64> setting = {""};
    build_info::fetch_text(build_info::kKeybinds, "Screenshot", "", setting.data(), sizeof(setting));
    const int fallback = default_key(snipping);
    auto by_name = [](const std::string& name) { return static_cast<int>(SDL_GetScancodeFromName(name.c_str())); };
    g_key = parse_key(setting.data(), fallback, by_name);
    if (core::at(setting, 0) && parse_key(setting.data(), 0, by_name) == 0)
        get_log("screenshot key: [Keybinds] Screenshot=%s isn't a key - using the default", setting.data());
    get_log("screenshot key: %s (scan code %d%s) - saves to %s", SDL_GetScancodeName(static_cast<SDL_Scancode>(g_key)),
            g_key,
            core::at(setting, 0) ? ", [Keybinds] Screenshot"
            : snipping ? ", F12: Windows' Snipping Tool takes Print Screen"
                       : "",
            folder_path().c_str());
    if (snipping && g_key == kPrintScreen)
        get_log(
            "screenshot key: Windows' Snipping Tool takes Print Screen (Settings > Accessibility > Keyboard): the game "
            "won't see it - turn that off or set [Keybinds] Screenshot=69 (F12)");
    os_layer::register_event_handler(handle_event);
}

void before_present(bool rendering)
{
    // Not original either - a frame series (start_series), also with the key off.
    tick_series(rendering);
    if (!g_key && !g_requested && !g_job && g_message.empty()) // (key off: only request_event_shot's and show_notice's)
        return;
    if (g_job && g_job->done) {
        const std::shared_ptr<Job> job = std::move(g_job);
        const std::string note = job->note.empty() ? "" : " for " + job->note;
        if (job->ok) {
            get_log("screenshot: saved %s%s (and %s) in %.0f ms%s", job->folder.c_str(), job->png.c_str(),
                    job->txt.c_str(), job->milliseconds, note.c_str());
            if (job->note.empty())
                g_message = "Screenshot saved: " + job->png;
        } else {
            get_log("screenshot: failed - %s%s", job->error.c_str(), note.c_str());
            if (job->note.empty())
                g_message = "Screenshot failed (see the log)";
        }
        if (job->note.empty())
            g_message_since = SDL_GetTicks();
    }
    // (one at a time: a press while the last is still being written waits for it)
    if (rendering && g_requested && !g_job) {
        g_requested = false;
        get_take();
        g_event_note.clear();
    }
    tick_message();
}

std::string start_series(int frames, const std::string& folder)
{
    if (g_series)
        return "a series is running";
    if (frames < 1 || frames > kMaxSeriesFrames)
        return "1 to " + std::to_string(kMaxSeriesFrames) + " frames";
    const std::string base = get_folder();
    if (base.empty())
        return "no screenshot folder (see the log)";
    std::string path = folder;
    for (char& c : path)
        if (c == '/')
            c = '\\';
    if (path.empty()) {
        std::time_t now = std::time(nullptr);
        std::tm local = {};
        localtime_s(&local, &now);
        path = series_folder_name(
            {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec});
    }
    const bool absolute = path.size() >= 2 && (core::at(path, 1) == ':' || (core::at(path, 0) == '\\' && core::at(path, 1) == '\\'));
    if (!absolute)
        path = base + path;
    if (path.back() != '\\')
        path += '\\';
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path), error);
    const DWORD attributes = GetFileAttributesA(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return "can't make the folder " + path;
    auto series = std::make_shared<Series>();
    series->folder = path;
    series->total = frames;
    g_series = series;
    series_result_state() = "running";
    get_log("screenshot: series of %d frames into %s", frames, path.c_str());
    return {};
}

void show_notice(const std::string& text)
{
    g_message = text;
    g_message_since = SDL_GetTicks();
    g_message_y = 100.0f;
}

bool request_event_shot(const std::string& note)
{
    if (g_job || g_requested)
        return false; // (one at a time)
    g_event_note = note;
    g_requested = true;
    return true;
}

bool series_running() { return g_series != nullptr; }

std::string series_result() { return series_result_state(); }

bool last_view(std::span<float, 3> position, std::span<float, 3> forward)
{
    if (!g_view.noted || frame_loop::frames_run() - g_view.frame >= 10)
        return false;
    for (int i = 0; i < 3; ++i) {
        core::at(position, i) = core::at(g_view.pos, i);
        core::at(forward, i) = core::at(g_view.forward, i);
    }
    return true;
}

void note_view(int viewport, std::span<const float, 3> position, std::span<const float, 3> forward)
{
    if (viewport != 0)
        return;
    for (int i = 0; i < 3; ++i) {
        core::at(g_view.pos, i) = core::at(position, i);
        core::at(g_view.forward, i) = core::at(forward, i);
    }
    g_view.frame = frame_loop::frames_run();
    g_view.noted = true;
}

} // namespace sys::screenshot
