// NOT ORIGINAL - the screenshot key (game/sys/screenshot.h).

#include "game/sys/screenshot.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <SDL2/SDL.h>

#include "build_info.h"
#include "game/core/crc.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_scenery.h"
#include "game/gfx/screenshot_capture.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/config.h"
#include "game/sys/mlp.h"
#include "game/sys/screenshot_files.h"
#include "game/sys/sdl_events.h"
#include "game/sys/sdl_window.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Sys::Screenshot {

namespace {

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu (the skate module's level in the front end)
constexpr int kLogLines = 20;
constexpr uint32_t kMessageMs = 2000;

int g_key;
std::atomic<bool> g_requested{false};

// The camera of viewport 0 as last drawn.
struct View {
    float pos[3], forward[3];
    uint32_t frame;
    bool noted;
} g_view;

// The writer: one picture at a time, on its own thread (made RGB, PNG-encoded, written with its sidecar). The main
// thread reads `done` and then the rest.
struct Job {
    Frame frame;
    std::string folder, png, txt, sidecar;
    std::atomic<bool> done{false};
    bool ok = false;
    std::string error;
    double milliseconds = 0.0;
};
std::shared_ptr<Job> g_job;

// The message for the player, held while a loading screen is up (for up to 10 s).
std::string g_message;
uint32_t g_messageSince;

// --- the game's state ---

uint8_t* SkateModule() { return GameVariable<uint8_t*>(0x007CE478); }
// The level loaded: its load script's checksum (the skate module's +0x378; load_mainmenu in the front end).
uint32_t LevelLoad() { return SkateModule() ? *reinterpret_cast<uint32_t*>(SkateModule() + 0x378) : 0; }
bool LoadingScreenUp()
{
    return GameVariable<uint8_t>(0x006FC49C) != 0 || GameVariable<uint32_t>(0x00786D08) != 0 ||
           GameVariable<uint8_t>(0x00786D04) != 0;
}
Mdl::FrontEnd* TheFrontEnd() { return GameVariable<Mdl::FrontEnd*>(0x007CE464); }
std::string NameOf(uint32_t checksum)
{
    if (const char* name = Script::FindChecksumName(checksum))
        return name;
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", checksum);
    return text;
}
std::string GameModeName()
{
    void* mode = GameVariable<void*>(0x007CE470);
    return mode ? NameOf(GameFunction<uint32_t(__thiscall*)(void*)>(0x005532A0)(mode)) : "";
}
// The local skater (the skate module's 0x00566520; null with none).
Obj::CCompositeObject* LocalSkater()
{
    return SkateModule() ? GameFunction<Obj::CCompositeObject*(__thiscall*)(void*)>(0x00566520)(SkateModule()) : nullptr;
}

std::string Vec(const float v[3], const char* format = "(%.1f, %.1f, %.1f)")
{
    char text[96];
    std::snprintf(text, sizeof(text), format, v[0], v[1], v[2]);
    return text;
}

std::string FileTime(const char* path)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data))
        return {};
    FILETIME local;
    SYSTEMTIME t;
    if (!FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &t))
        return {};
    char text[64];
    std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u, %llu bytes", t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond,
                  (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
    return text;
}

// Everything the sidecar says, read on the main thread (every field safe when it isn't there).
std::vector<Section> GatherSections(const Frame& frame, const std::string& png, const std::string& folder,
                                    const std::string& taken)
{
    std::vector<Section> sections;
    char text[256];

    Section shot{"screenshot", {}};
    shot.fields.push_back({"file", png});
    shot.fields.push_back({"folder", folder});
    shot.fields.push_back({"taken", taken});
    std::snprintf(text, sizeof(text), "%d x %d (%s)", frame.width, frame.height, FormatName(frame.format));
    shot.fields.push_back({"image", text});
    int w = 0, h = 0;
    if (SDL_Window* window = Sys::SdlWindow())
        SDL_GetWindowSize(window, &w, &h);
    if (w > 0 && h > 0) {
        std::snprintf(text, sizeof(text), "%d x %d", w, h);
        shot.fields.push_back({"window", text});
    } else {
        shot.fields.push_back({"window", ""});
    }
    sections.push_back(shot);

    Section build{"build", {}};
    char exe[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    const char* exeName = std::strrchr(exe, '\\') ? std::strrchr(exe, '\\') + 1 : exe;
    build.fields.push_back({"exe", exeName});
    build.fields.push_back({"exe file", FileTime(exe)});
    HMODULE dll = nullptr;
    char dllPath[MAX_PATH] = "";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&GatherSections), &dll))
        GetModuleFileNameA(dll, dllPath, MAX_PATH);
    build.fields.push_back({"dll", dllPath[0] ? std::string(std::strrchr(dllPath, '\\') ? std::strrchr(dllPath, '\\') + 1 : dllPath) : ""});
    build.fields.push_back({"dll file", dllPath[0] ? FileTime(dllPath) : ""});
    build.fields.push_back({"dll commit", THUG2_BUILD_COMMIT});
    sections.push_back(build);

    Section game{"game", {}};
    uint32_t load = LevelLoad();
    bool inLevel = load && load != kLoadMainMenu;
    bool loading = LoadingScreenUp();
    game.fields.push_back({"where", !load ? "starting up" : inLevel ? "level" : "front end"});
    game.fields.push_back({"level load script", load ? NameOf(load) : ""});
    uint32_t scene = Front::LevelEditor::Scenery::SceneName(Front::LevelEditor::Scenery::MainScene());
    game.fields.push_back({"scene", scene ? NameOf(scene) : ""});
    game.fields.push_back({"game mode", GameModeName()});
    Mdl::FrontEnd* frontEnd = TheFrontEnd();
    game.fields.push_back({"paused", frontEnd ? (frontEnd->m_paused ? "yes" : "no") : ""});
    game.fields.push_back({"loading screen", loading ? "yes" : "no"});
    game.fields.push_back({"level editor", Front::LevelEditor::Active() ? "open" : "closed"});
    game.fields.push_back({"frames run", std::to_string(Mlp::FramesRun())});
    sections.push_back(game);

    Section skater{"skater", {}};
    if (inLevel && !loading) {
        if (Obj::CCompositeObject* s = LocalSkater()) {
            float pos[3] = {s->m_pos.x, s->m_pos.y, s->m_pos.z};
            skater.fields.push_back({"position", Vec(pos)});
            const Mth::Vector& at = s->m_matrix.row[2];
            double heading = std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)) * 180.0 / 3.14159265358979;
            std::snprintf(text, sizeof(text), "%.1f degrees (0 = +z, 90 = +x)", heading);
            skater.fields.push_back({"heading", text});
        }
    }
    sections.push_back(skater);

    Section camera{"camera", {}};
    if (g_view.noted && Mlp::FramesRun() - g_view.frame < 10) {
        camera.fields.push_back({"position", Vec(g_view.pos)});
        camera.fields.push_back({"looking along", Vec(g_view.forward, "(%.3f, %.3f, %.3f)")});
    }
    sections.push_back(camera);

    if (Front::LevelEditor::Active()) {
        Section editor{"level editor", {}};
        for (auto& [key, value] : Front::LevelEditor::DescribeForScreenshot())
            editor.fields.push_back({key, value});
        sections.push_back(editor);
    }
    return sections;
}

// --- the message ---

void ShowMessage(const std::string& text)
{
    if (Front::LevelEditor::Active()) {
        Front::LevelEditor::Hud::Message(text, kMessageMs / 1000.0);
        return;
    }
    // A panel message (as the restart tester's), once the game's scripts are up.
    static const uint32_t kScript = Crc::GenerateCRCFromString("create_panel_message");
    if (!LevelLoad() || !Script::ScriptExists(kScript))
        return;
    auto crc = [](const char* name) { return Crc::GenerateCRCFromString(name); };
    Script::CStruct* params = Script::NewStruct()->Init();
    params->AddString(crc("text"), text.c_str());
    params->AddChecksum(crc("id"), crc("thug2_decomp_screenshot_message"));
    params->AddPair(crc("pos"), 320.0f, 40.0f);
    params->AddInteger(crc("time"), static_cast<int>(kMessageMs));
    Script::SpawnScript(kScript, params, 0, nullptr, 0, 0, false, false, false, false);
    params->Clear();
    Script::DeleteStruct(params);
}

void TickMessage()
{
    if (g_message.empty())
        return;
    if (SDL_GetTicks() - g_messageSince > 10000) {
        g_message.clear();
        return;
    }
    if (LoadingScreenUp())
        return;
    ShowMessage(g_message);
    g_message.clear();
}

// --- taking and writing ---

void Write(std::shared_ptr<Job> job)
{
    LARGE_INTEGER start, end, frequency;
    QueryPerformanceCounter(&start);
    std::vector<uint8_t> rgb, png;
    if (!ToRgb(job->frame, &rgb)) {
        job->error = "the pixels couldn't be read";
    } else if (!EncodePng(rgb.data(), job->frame.width, job->frame.height, &png)) {
        job->error = "the PNG couldn't be made";
    } else {
        auto writeNew = [](const std::string& path, const void* data, size_t size) {
            HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                return false;
            DWORD written = 0;
            bool ok = WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) && written == size;
            CloseHandle(file);
            return ok;
        };
        std::string pngPath = job->folder + job->png, txtPath = job->folder + job->txt;
        if (!writeNew(pngPath, png.data(), png.size()))
            job->error = "couldn't write " + pngPath + " (error " + std::to_string(GetLastError()) + ")";
        else if (!writeNew(txtPath, job->sidecar.data(), job->sidecar.size()))
            job->error = "couldn't write " + txtPath + " (error " + std::to_string(GetLastError()) + ")";
        else
            job->ok = true;
    }
    job->frame.bytes.clear();
    job->frame.bytes.shrink_to_fit();
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    job->milliseconds = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
    job->done = true;
}

// The folder (ending in a backslash): [features] screenshot_dir or Screenshots, in the game folder unless absolute.
std::string FolderPath()
{
    char setting[MAX_PATH] = "";
    Config::GetString("features", "screenshot_dir", "", setting, sizeof(setting));
    std::string folder = setting;
    while (!folder.empty() && (folder.back() == ' ' || folder.back() == '\t'))
        folder.pop_back();
    if (folder.empty())
        folder = "Screenshots";
    bool absolute = folder.size() >= 2 && (folder[1] == ':' || (folder[0] == '\\' && folder[1] == '\\'));
    if (!absolute)
        folder = GameFolderPath(folder.c_str());
    for (char& c : folder)
        if (c == '/')
            c = '\\';
    if (folder.back() != '\\')
        folder += '\\';
    return folder;
}

// The folder, made if it isn't there; "" if it can't be.
std::string Folder()
{
    std::string folder = FolderPath();
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(folder), error);
    DWORD attributes = GetFileAttributesA(folder.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        Log("screenshot: can't make the folder %s", folder.c_str());
        return {};
    }
    return folder;
}

void Take()
{
    Frame frame;
    std::string error;
    if (!Gfx::ScreenshotCapture::CaptureBackBuffer(&frame, &error)) {
        Log("screenshot: not taken - %s", error.c_str());
        g_message = "Screenshot failed: " + error;
        g_messageSince = SDL_GetTicks();
        return;
    }
    std::string folder = Folder();
    if (folder.empty()) {
        g_message = "Screenshot failed: no folder (see the log)";
        g_messageSince = SDL_GetTicks();
        return;
    }
    std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_s(&local, &now);
    Stamp stamp{local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec};
    std::string png = PickName(FileStem(stamp), [&folder](const std::string& name) {
        return GetFileAttributesA((folder + name).c_str()) != INVALID_FILE_ATTRIBUTES;
    });
    if (png.empty()) {
        Log("screenshot: no free name in %s", folder.c_str());
        return;
    }
    char taken[64];
    std::snprintf(taken, sizeof(taken), "%04d-%02d-%02d %02d:%02d:%02d", stamp.year, stamp.month, stamp.day, stamp.hour,
                  stamp.minute, stamp.second);
    auto job = std::make_shared<Job>();
    job->folder = folder;
    job->png = png;
    job->txt = SidecarName(png);
    job->sidecar = FormatSidecar("THUG2 decomp screenshot " + png, GatherSections(frame, png, folder, taken),
                                 LogTail(kLogLines));
    job->frame = std::move(frame);
    g_job = job;
    std::thread(Write, job).detach();
}

void HandleEvent(SDL_Event* event)
{
    if (event->type == SDL_KEYDOWN && !event->key.repeat && static_cast<int>(event->key.keysym.scancode) == g_key)
        g_requested = true;
}

// Whether Windows' Snipping Tool takes the Print Screen key (its setting; Windows 11 turns it on by default).
bool SnippingTakesPrintScreen()
{
    DWORD value = 0, size = sizeof(value);
    return RegGetValueA(HKEY_CURRENT_USER, "Control Panel\\Keyboard", "PrintScreenKeyForSnippingEnabled",
                        RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
           value != 0;
}

} // namespace

bool Enabled()
{
    static bool on = ConfigInt("features", "screenshots", 1) != 0;
    return on;
}

int Key() { return g_key; }

void Init()
{
    if (!Enabled()) {
        Log("screenshot key: off ([features] screenshots=0)");
        return;
    }
    bool snipping = SnippingTakesPrintScreen();
    char setting[64] = "";
    Config::GetString(Config::kKeybinds, "Screenshot", "", setting, sizeof(setting));
    int fallback = DefaultKey(snipping);
    auto byName = [](const std::string& name) { return static_cast<int>(SDL_GetScancodeFromName(name.c_str())); };
    g_key = ParseKey(setting, fallback, byName);
    if (setting[0] && ParseKey(setting, 0, byName) == 0)
        Log("screenshot key: [Keybinds] Screenshot=%s isn't a key - using the default", setting);
    Log("screenshot key: %s (scan code %d%s) - saves to %s", SDL_GetScancodeName(static_cast<SDL_Scancode>(g_key)),
        g_key, setting[0] ? ", [Keybinds] Screenshot" : snipping ? ", F12: Windows' Snipping Tool takes Print Screen" : "",
        FolderPath().c_str());
    if (snipping && g_key == kPrintScreen)
        Log("screenshot key: Windows' Snipping Tool takes Print Screen (Settings > Accessibility > Keyboard): the game "
            "won't see it - turn that off or set [Keybinds] Screenshot=69 (F12)");
    Platform::RegisterEventHandler(HandleEvent);
}

void BeforePresent(bool rendering)
{
    if (!g_key)
        return;
    if (g_job && g_job->done) {
        std::shared_ptr<Job> job = std::move(g_job);
        if (job->ok) {
            Log("screenshot: saved %s%s (and %s) in %.0f ms", job->folder.c_str(), job->png.c_str(), job->txt.c_str(),
                job->milliseconds);
            g_message = "Screenshot saved: " + job->png;
        } else {
            Log("screenshot: failed - %s", job->error.c_str());
            g_message = "Screenshot failed (see the log)";
        }
        g_messageSince = SDL_GetTicks();
    }
    // (one at a time: a press while the last is still being written waits for it)
    if (rendering && g_requested && !g_job) {
        g_requested = false;
        Take();
    }
    TickMessage();
}

void NoteView(int viewport, const float position[3], const float forward[3])
{
    if (viewport != 0)
        return;
    for (int i = 0; i < 3; ++i) {
        g_view.pos[i] = position[i];
        g_view.forward[i] = forward[i];
    }
    g_view.frame = Mlp::FramesRun();
    g_view.noted = true;
}

} // namespace Sys::Screenshot
