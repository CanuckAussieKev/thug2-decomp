// NOT ORIGINAL - the new front end (ours): see new_frontend.h. The glue to the game: when it's up and over which
// original screen, its input, what its menus ask for (hand-offs, the ini, levels), its pictures and fonts, its ImGui
// context, the automation channel's commands.

#include "game/front/new_frontend.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "game/core/crc.h"
#include "game/file/file.h"
#include "game/front/automation.h"
#include "game/front/data_overlay.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/front/level_editor_playtest.h"
#include "game/front/level_editor_ui_backend.h"
#include "game/front/mouse_menus.h"
#include "game/front/new_frontend_draw.h"
#include "game/front/new_frontend_game.h"
#include "game/front/new_frontend_mirror.h"
#include "game/front/new_frontend_model.h"
#include "game/front/new_frontend_options.h"
#include "game/front/new_frontend_text.h"
#include "game/front/restart_tester.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/sprite_element.h"
#include "game/front/start_level.h"
#include "game/front/text_element.h"
#include "game/gfx/font.h"
#include "game/gfx/glyphs.h"
#include "game/gfx/hd_ui.h"
#include "game/net/online_identity.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/sio/device.h"
#include "game/core/str.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/config.h"
#include "game/sys/platform_system.h"
#include "game/sys/sdl_events.h"
#include "game/sys/sdl_window.h"
#include "hook/game.h"
#include "hook/log.h"
#include "imgui.h"
#include "game/gfx/loading_bar_cfuncs.h"
#include "game/gfx/device.h"
#include "game/gfx/engine_glue.h"

namespace Front::NewFrontend {

void ContinueLastLevel(Mdl::FrontEnd* frontEnd); // (below)

namespace {

namespace UiBackend = Front::LevelEditor::UiBackend;
using GameScreens::Anchor;

uint32_t Checksum(const char* name) { return Crc::GenerateCRCFromString(name); }

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;       // load_mainmenu (the skate module's level in the front end)
constexpr uint32_t kSkater = 0x5B8AB877;             // Skater (the local skater's alias)
constexpr uint32_t kSkaterState = 0x43686585;        // SkaterState
constexpr uint32_t kSkaterScore = 0xD262802F;        // SkaterScore
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;  // SkaterCorePhysics

// --- the game's state ---

uint8_t* SkateModule() { return reinterpret_cast<uint8_t*>(Mdl::g_skateModule); }
// The level loaded: its load script's checksum (the skate module's +0x378; load_mainmenu in the front end).
uint32_t LevelLoad() { return SkateModule() ? *reinterpret_cast<uint32_t*>(SkateModule() + 0x378) : 0; }
bool InFrontEnd() { return LevelLoad() == kLoadMainMenu; }
// The skate module's career (+0x20; null before the module is up).
Obj::CSkaterCareer* Career()
{
    return SkateModule() ? *reinterpret_cast<Obj::CSkaterCareer**>(SkateModule() + 0x20) : nullptr;
}
Mdl::FrontEnd* TheFrontEnd() { return Mdl::g_frontEndInstance; }
bool Paused() { return TheFrontEnd() && TheFrontEnd()->m_paused; }
bool LoadingScreenUp()
{
    return Nx::g_loadingScreenShown != 0 || Nx::g_loadingBarTimer != 0 ||
           static_cast<uint8_t>(Nx::g_loadingScreenUp) != 0;
}

bool IsScript(uint32_t name)
{
    Script::CSymbolTableEntry* symbol = Script::LookUpSymbol(name);
    return symbol && symbol->type == Script::ESYMBOLTYPE_QSCRIPT;
}

// A script integer by name (-1: none).
int ScriptInteger(const std::string& name)
{
    if (name.empty())
        return -1;
    Script::CSymbolTableEntry* symbol = Script::LookUpSymbol(Checksum(name.c_str()));
    return symbol && symbol->type == Script::ESYMBOLTYPE_INTEGER ? symbol->integer : -1;
}


// --- the setting and the files ---

std::string IniPath() { return GameFolderPath(Config::kFileName); }

std::string IniString(const char* key, const char* fallback)
{
    char value[512] = {};
    GetPrivateProfileStringA("frontend", key, fallback, value, sizeof(value), IniPath().c_str());
    return value;
}

bool FileExists(const std::string& path)
{
    DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::string WindowsFonts()
{
    char windows[MAX_PATH] = {};
    UINT n = GetWindowsDirectoryA(windows, MAX_PATH);
    return std::string(windows, n) + "\\Fonts\\";
}

// A font named in the ini (or the default list): a full path as it is, else the overlay's frontend\fonts, else
// Windows' fonts folder. "" if none of the names is there.
std::string FindFont(const std::vector<std::string>& names)
{
    std::string ours = DataOverlay::Folder() + "\\frontend\\fonts\\";
    std::string windows = WindowsFonts();
    for (const std::string& name : names) {
        if (name.empty())
            continue;
        if (name.find(':') != std::string::npos || name.rfind("\\\\", 0) == 0) {
            if (FileExists(name))
                return name;
            continue;
        }
        if (FileExists(ours + name))
            return ours + name;
        if (FileExists(windows + name))
            return windows + name;
    }
    return {};
}

Paint::FontFiles ResolveFonts()
{
    Paint::FontFiles files;
    files.display = FindFont({IniString("display_font", ""), "Anton-Regular.ttf", "bahnschrift.ttf", "impact.ttf"});
    files.mono = FindFont({IniString("mono_font", ""), "IBMPlexMono-Regular.ttf", "consola.ttf"});
    files.body = FindFont({IniString("body_font", ""), "IBMPlexSans-Regular.ttf", "segoeui.ttf"});
    // Bahnschrift (Windows' DIN): the default instance of its variable font is regular width and weight - drawn
    // narrower and heavier to come nearer Anton; Anton and Impact are already condensed and heavy.
    std::string lower = files.display;
    for (char& ch : lower)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    bool bahnschrift = lower.find("bahnschrift") != std::string::npos;
    files.condense = static_cast<float>(std::atoi(IniString("display_condense", bahnschrift ? "84" : "100").c_str())) / 100.0f;
    files.bold = std::atoi(IniString("display_bold", bahnschrift ? "1" : "0").c_str()) ? 0.022f : 0.0f;
    Log("new front end: fonts - display %s (condensed %.0f%%%s), mono %s, body %s",
        files.display.empty() ? "(none)" : files.display.c_str(), files.condense * 100.0f, files.bold > 0 ? ", heavier" : "",
        files.mono.empty() ? "(none)" : files.mono.c_str(), files.body.empty() ? "(none)" : files.body.c_str());
    return files;
}

std::string ReadText(const std::string& path)
{
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || !file)
        return {};
    std::string text;
    char buffer[4096];
    for (size_t n; (n = std::fread(buffer, 1, sizeof(buffer), file)) > 0;)
        text.append(buffer, n);
    std::fclose(file);
    return text;
}

// The text written next to `path` and moved over it in one step (a crash part-way leaves the old file).
bool WriteAtomically(const std::string& path, const std::string& text)
{
    std::string temporary = path + ".new";
    FILE* file = nullptr;
    if (fopen_s(&file, temporary.c_str(), "wb") != 0 || !file)
        return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    ok = std::fflush(file) == 0 && ok;
    std::fclose(file);
    if (!ok || !MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(temporary.c_str());
        return false;
    }
    return true;
}

std::string StatePath() { return GameFolderPath("thug2-decomp-frontend.txt"); }

// --- the state ---

enum class State {
    Idle,      // not up (a level being played, the game's screens restyled or as they are)
    Shown,     // our screen a menu's item asked for: drawn, with the input
    Away,      // the game's screens up (restyled or as they are)
    Launching, // a level started: the art up until it loads
};
const char* StateName(State state)
{
    switch (state) {
    case State::Idle: return "idle";
    case State::Shown: return "shown";
    case State::Away: return "away";
    case State::Launching: return "launching";
    }
    return "?";
}
State s_state = State::Idle;
float s_stateTime = 0.0f;
Anchor s_anchor = Anchor::None;       // the game's menu up when ours came up (the log, the status)
// Our screen a menu's item asked for (NewFrontend_Open: the overlay's CHANGE LEVEL / OPTIONS / LEVEL EDITOR items, the
// skate shop's Play Level) - shown once, over whatever is up, on our root (Main in the front end, Pause in a level);
// back at the root, ours goes aside again (the restyled menu it came from under it, or `s_openBack` spawned when that
// menu went - the skate shop's).
Screen s_openScreen = Screen::Main; // (Main: none asked for)
LevelsFor s_openLevels = LevelsFor::FreeSkate;
uint32_t s_openBack = 0;           // (a script spawned when it is closed with Back; 0: none)
uint32_t s_shownBack = 0;          // (the one of the screen shown now)
Screen s_openRoot = Screen::Main;
bool s_rootHidden = false;            // the original's screen elements hidden under our pause menu
uint32_t s_launchLevel = 0;           // the level up when a launch started (in a level: done when it changes)

Catalogue s_catalogue;
bool s_catalogueTried = false, s_catalogueOk = false;
Menu s_menu;
SavedState s_saved;
uint32_t s_lastLoad = 0;

std::vector<SDL_Event> s_events;
std::vector<Paint::Hit> s_hits;
bool s_guard = false; // (the game's input held back until the keys that chose are let go)
float s_guardTime = 0.0f;
int s_buttonsDown = 0;
std::string s_skaterName;

// Controller directions held (the d-pad, the left stick): repeated as the keyboard repeats.
struct Repeat {
    int direction = -1; // an Input (Up / Down / Left / Right), -1 none
    float wait = 0.0f;
};
Repeat s_repeat;
int s_stickX = 0, s_stickY = 0;
constexpr float kRepeatDelay = 0.35f, kRepeatEvery = 0.09f;

std::chrono::steady_clock::time_point s_lastTick;
bool s_ticked = false;

// The Level Editor chosen on the Create screen: the level loads, then Start pauses it and the pause menu's LEVEL
// EDITOR opens the editor (as -edit does - game/front/start_level.h).
enum class Edit { Off, WaitLevel, WaitPause };
Edit s_edit = Edit::Off;
float s_editTime = 0.0f;
int s_editFrames = 0, s_editPresses = 0;

// The options' state: what the ini said at start-up (a Restart option changed since is "pending").
std::vector<std::string> s_startValues;
std::vector<bool> s_restartPending;
std::string s_optionsNote;

// --- the session in a level (the pause menu's panel) ---

struct SessionState {
    uint32_t level = 0;
    float seconds = 0.0f;
    int bestCombo = 0;
    std::set<std::string> gapsLanded;
    int gapsTotal = 0;
    LevelEditor::PlayTest::ComboTracker combo;
    LevelEditor::PlayTest::GapTracker gaps;
};
SessionState s_session;

template <typename T> T At(const void* base, uint32_t offset)
{
    return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(base) + offset);
}

void UpdateSession(float dt)
{
    uint32_t level = LevelLoad();
    if (level != s_session.level) {
        s_session = SessionState{};
        s_session.level = level;
    }
    if (!level || level == kLoadMainMenu || Paused() || LevelEditor::Active())
        return;
    auto* skater = static_cast<Obj::CCompositeObject*>(Obj::FindObject(kSkater));
    if (!skater)
        return;
    s_session.seconds += dt;
    // The combo's points pending (the score's +0x14 times its multiplier +0x60 - level_editor_playtest.h), bails.
    int pending = 0;
    bool bailing = false;
    if (Obj::CBaseComponent* state = skater->GetComponent(kSkaterState))
        bailing = At<uint8_t>(state, 0x70) != 0;
    if (Obj::CBaseComponent* score = skater->GetComponent(kSkaterScore))
        if (const uint8_t* s = At<const uint8_t*>(score, 0x18)) {
            double points = static_cast<double>(At<int>(s, 0x14)) * At<float>(s, 0x60);
            if (points > 0.0 && points < 2.0e9)
                pending = static_cast<int>(points);
        }
    LevelEditor::PlayTest::ComboEnd end;
    if (s_session.combo.Update(pending, bailing, &end) && end.landed)
        s_session.bestCombo = std::max(s_session.bestCombo, end.points);
    // The gaps (the skate module's checklist: the level's).
    std::vector<LevelEditor::PlayTest::GapRead> read;
    uint8_t* skate = SkateModule();
    if (Game::CGapChecklist* list = skate ? static_cast<Game::CGapChecklist*>(reinterpret_cast<Mdl::Skate*>(skate)->GetGapChecklist()) : nullptr) {
        int guard = 0;
        for (Mem::ListNode* node = list->m_gaps.next; node && node != &list->m_gaps && ++guard < 4096; node = node->next) {
            if (node->data == reinterpret_cast<void*>(-1))
                break;
            auto* gap = reinterpret_cast<Game::CGapCheck*>(node);
            read.push_back({gap->m_name.c_str() ? gap->m_name.c_str() : "", gap->m_pending, gap->m_count, gap->m_score});
        }
    }
    s_session.gapsTotal = static_cast<int>(read.size());
    for (const LevelEditor::PlayTest::GapChange& change : s_session.gaps.Update(read))
        if (change.kind == LevelEditor::PlayTest::GapChange::Landed)
            s_session.gapsLanded.insert(change.name);
}

// --- the pictures ---

struct CachedArt {
    Paint::Art art;
    uint32_t used = 0;
    bool failed = false;
};
std::map<std::string, CachedArt> s_art;
uint32_t s_artClock = 0;
constexpr size_t kArtCached = 10;

void FreeArt()
{
    for (auto& [name, cached] : s_art)
        UiBackend::ReleaseTexture(cached.art.texture);
    s_art.clear();
}

// The image file (the game's .img.xbx: a 32-byte header, a palette for 8-bit images, the texels - swizzled when both
// sides are powers of two; an upscaled loading screen may be DXT1 blocks - game/gfx/hd_ui.h) as a texture.
Paint::Art LoadImageFile(const std::string& name)
{
    std::string path = "images\\" + name + ".img.xbx";
    File::Handle file = File::Open(path.c_str(), "rb");
    if (!file)
        return {};
    uint32_t size = File::GetFileSize(file);
    std::vector<uint8_t> data(size);
    bool read = size > 0x20 && File::Read(data.data(), static_cast<int>(size), 1, file) > 0;
    File::Close(file);
    if (!read)
        return {};
    auto u32 = [&](size_t at) {
        uint32_t v;
        std::memcpy(&v, data.data() + at, 4);
        return v;
    };
    uint32_t width = u32(8), height = u32(12), format = u32(16), marker = u32(20), paletteSize = u32(28);
    if (width == 0 || height == 0 || width > 8192 || height > 8192)
        return {};
    const uint8_t* texels = data.data() + 0x20;
    size_t left = data.size() - 0x20;
    Paint::Art art;
    art.width = static_cast<float>(width);
    art.height = static_cast<float>(height);
    // (a loading screen's rows are stored bottom first: Nx::DrawLoadingScreenPicture puts v 1 at the screen's top)
    art.bottomUp = true;
    if (format == Nx::HdUi::kFormatDxt1 && Nx::HdUi::ImageScale(marker) != 1 && width % 4 == 0 && height % 4 == 0) {
        int rowBytes = static_cast<int>(width / 4 * 8), rows = static_cast<int>(height / 4);
        if (left < static_cast<size_t>(rowBytes) * rows)
            return {};
        art.texture = UiBackend::CreateTexture(static_cast<int>(width), static_cast<int>(height),
                                               UiBackend::TextureFormat::Dxt1, texels, rowBytes, rows);
        return art;
    }
    uint32_t bits = format == 0 ? 32 : format == 2 ? 16 : format == 0x13 ? 8 : format;
    bool palettized = format == 0x13 || (format != 0 && format != 2 && format < 0x10);
    if (bits != 32 && !(palettized && bits == 8))
        return {}; // (16-bit and 4-bit images: no loading screen is one)
    const uint8_t* palette = nullptr;
    if (palettized) {
        size_t paletteBytes = paletteSize ? paletteSize : 0x400;
        if (left < paletteBytes)
            return {};
        palette = texels;
        texels += paletteBytes;
        left -= paletteBytes;
    }
    size_t count = static_cast<size_t>(width) * height;
    if (left < count * (bits / 8))
        return {};
    bool powers = (width & (width - 1)) == 0 && (height & (height - 1)) == 0;
    std::vector<uint8_t> plain;
    const uint8_t* rowsData = texels;
    if (powers) {
        plain.resize(count * (bits / 8));
        Platform::Unswizzle(plain.data(), texels, static_cast<int>(width), static_cast<int>(height),
                            static_cast<int>(bits / 8));
        rowsData = plain.data();
    }
    std::vector<uint8_t> bgra(count * 4);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* px = palettized ? palette + rowsData[i] * 4 : rowsData + i * 4;
        bgra[i * 4 + 0] = px[0];
        bgra[i * 4 + 1] = px[1];
        bgra[i * 4 + 2] = px[2];
        bgra[i * 4 + 3] = 0xFF; // (a full-bleed picture: opaque - the files' alpha is the PS2's half scale)
    }
    art.texture = UiBackend::CreateTexture(static_cast<int>(width), static_cast<int>(height),
                                           UiBackend::TextureFormat::Bgra32, bgra.data(), static_cast<int>(width * 4),
                                           static_cast<int>(height));
    return art;
}

Paint::Art ArtFor(const std::string& image)
{
    auto it = s_art.find(image);
    if (it == s_art.end()) {
        if (s_art.size() >= kArtCached) {
            auto oldest = s_art.begin();
            for (auto i = s_art.begin(); i != s_art.end(); ++i)
                if (i->second.used < oldest->second.used)
                    oldest = i;
            UiBackend::ReleaseTexture(oldest->second.art.texture);
            s_art.erase(oldest);
        }
        CachedArt cached;
        cached.art = LoadImageFile(image);
        cached.failed = !cached.art.texture;
        if (cached.failed)
            Log("new front end: no picture images\\%s.img.xbx", image.c_str());
        it = s_art.emplace(image, cached).first;
    }
    it->second.used = ++s_artClock;
    return it->second.art;
}

Paint::Records RecordsFor(const Level& level)
{
    Paint::Records records;
    int number = level.levelNum >= 0 ? level.levelNum : ScriptInteger(level.levelNumName);
    Obj::CSkaterCareer* career = Career();
    // (the career's records are THUG2's own levels': 21 of them)
    if (!career || number < 0 || number >= Obj::CSkaterCareer::kLevels || !level.gameTag.empty() ||
        level.levelNumName.empty())
        return records;
    int goals = career->CountGoals(number);
    if (goals > 0)
        records.goals = goals;
    if (Game::CGapChecklist* gaps = career->GetGapChecklist(number)) {
        int total = gaps->Count();
        if (total > 0) {
            int got = 0;
            for (int i = 0; i < total; ++i)
                got += gaps->GetGapCount(i) > 0 ? 1 : 0;
            records.gapsTotal = total;
            records.gapsGot = got;
        }
    }
    return records;
}

// [frontend] respect_locks (0, the default: every level open in Free Skate - the user's wish; 1: the game's level
// select's rules).
bool RespectLocks()
{
    return GetPrivateProfileIntA("frontend", "respect_locks", 0, IniPath().c_str()) != 0;
}

// Whether a level is unlocked, as the game's level select decides it outside a career (level_select_menu_add_item,
// gamemenu_levelselect.qb): every level when all_levels_unlocked is 1 (the cheat); else its seen_flag when it has one
// (level_select_should_use_seen_flags - free skate and single session use the seen flags), else its flag; no flag:
// open. Every level when respect_locks is off.
bool Unlocked(const Level& level)
{
    if (!RespectLocks() || ScriptInteger("all_levels_unlocked") == 1)
        return true;
    int flag = ScriptInteger(level.seenFlag.empty() ? level.flag : level.seenFlag);
    Obj::CSkaterCareer* career = Career();
    return flag < 0 || !career || career->GetGlobalFlag(flag);
}

std::vector<SectionView> Views()
{
    std::vector<SectionView> views;
    for (const Section& section : s_catalogue.sections) {
        SectionView view;
        for (size_t i = 0; i < section.levels.size(); ++i) {
            const Level& level = section.levels[i];
            bool unlocked = Unlocked(level);
            // (the game's select drops hide_when_locked outside a career when it uses the seen flags: listed, locked)
            if (!unlocked && level.hideWhenLocked && level.seenFlag.empty())
                continue;
            view.levels.push_back(static_cast<int>(i));
            view.locked.push_back(!unlocked);
        }
        views.push_back(std::move(view));
    }
    return views;
}

void LoadCatalogue()
{
    if (s_catalogueTried)
        return;
    s_catalogueTried = true;
    std::string path = DataOverlay::Folder() + "\\frontend\\levels.json";
    std::string text = ReadText(path);
    std::string error;
    if (text.empty()) {
        Log("new front end: no %s (make_menu_level.ts makes it) - Free Skate goes to the original level select",
            path.c_str());
    } else if (!ParseCatalogue(text, &s_catalogue, &error)) {
        Log("new front end: %s - %s; Free Skate goes to the original level select", path.c_str(), error.c_str());
        s_catalogue = Catalogue{};
    } else {
        s_catalogueOk = true;
        size_t levels = 0;
        for (const Section& s : s_catalogue.sections)
            levels += s.levels.size();
        Log("new front end: %zu sections, %zu levels from %s", s_catalogue.sections.size(), levels, path.c_str());
    }
    s_saved = ParseState(ReadText(StatePath()));
}

void SaveState()
{
    WriteAtomically(StatePath(), FormatState(s_saved));
}

// The last level played: whichever menu started it, once it's the level loaded.
void NoteLevelLoaded()
{
    uint32_t load = LevelLoad();
    if (load == s_lastLoad)
        return;
    s_lastLoad = load;
    if (!load || load == kLoadMainMenu || !s_catalogueOk)
        return;
    for (const Section& section : s_catalogue.sections)
        for (const Level& level : section.levels)
            if (Checksum(level.load.c_str()) == load) {
                if (s_saved.lastLevel != level.load) {
                    s_saved.lastLevel = level.load;
                    SaveState();
                }
                return;
            }
}

std::string SkaterName()
{
    uint32_t script = Checksum("select_skater_get_current_skater_name");
    if (!IsScript(script))
        return {};
    Script::CStruct results;
    results.Init();
    Script::RunScriptOnce(script, nullptr, nullptr, &results);
    const char* name = nullptr;
    std::string out;
    // (the profile's info - GetSkaterProfileInfo's display_name - is among the script's parameters after it)
    if ((results.GetString("display_name", &name, 0) || results.GetString("current_skater", &name, 0)) && name)
        out = name;
    results.Clear();
    return out;
}

void Sound(const char* script, const char* flag = nullptr)
{
    uint32_t name = Checksum(script);
    if (!IsScript(name))
        return;
    Script::CStruct params;
    params.Init();
    if (flag)
        params.AddChecksum(0u, Checksum(flag));
    Script::RunScript(name, &params, nullptr, false, script);
    params.Clear();
}

// --- the options ---

void LoadOptionValues()
{
    std::string text = ReadText(IniPath());
    const std::vector<Options::Spec>& specs = Options::Specs();
    std::vector<int> choices(specs.size());
    bool first = s_startValues.empty();
    if (first) {
        s_startValues.resize(specs.size());
        s_restartPending.assign(specs.size(), false);
    }
    for (size_t i = 0; i < specs.size(); ++i) {
        std::string value;
        bool present = Options::IniGet(text, specs[i].section, specs[i].key, &value);
        choices[i] = Options::ChoiceOf(specs[i], value, present);
        if (first)
            s_startValues[i] = specs[i].choices[choices[i]].value;
    }
    s_menu.SetOptionValues(std::move(choices));
}

void ApplyOption(int option, int choice)
{
    const std::vector<Options::Spec>& specs = Options::Specs();
    if (option < 0 || option >= static_cast<int>(specs.size()))
        return;
    const Options::Spec& spec = specs[option];
    const char* value = spec.choices[choice].value;
    std::string path = IniPath();
    std::string text = ReadText(path);
    std::string changed = Options::IniSet(text, spec.section, spec.key, value);
    if (!WriteAtomically(path, changed)) {
        Log("new front end: couldn't write %s ([%s] %s=%s)", path.c_str(), spec.section, spec.key, value);
        s_optionsNote = Text::kCouldntSave;
        return;
    }
    Log("new front end: options - [%s] %s=%s written to %s", spec.section, spec.key, value, path.c_str());
    s_optionsNote.clear();
    if (spec.apply == Options::Apply::Live) {
        bool on = std::strcmp(value, "0") != 0;
        if (std::strcmp(spec.key, "mouse_menus") == 0)
            MouseMenus::Enabled() = on;
        else if (std::strcmp(spec.key, "restart_tester") == 0)
            RestartTester::Enabled() = on;
        else if (std::strcmp(spec.key, "level_editor") == 0)
            LevelEditor::Enabled() = on;
        else if (std::strcmp(spec.key, "respect_locks") == 0)
            s_menu.SetViews(Views());
    } else if (option < static_cast<int>(s_restartPending.size())) {
        s_restartPending[option] = s_startValues[option] != value;
    }
}

// The level being played, as the data names it (null if it isn't there).
const Level* LevelPlayed(const Section** section)
{
    uint32_t load = LevelLoad();
    for (const Section& s : s_catalogue.sections)
        for (const Level& level : s.levels)
            if (Checksum(level.load.c_str()) == load) {
                if (section)
                    *section = &s;
                return &level;
            }
    return nullptr;
}

// --- the state machine ---

void SetState(State state)
{
    if (s_state == state)
        return;
    Log("new front end: %s -> %s%s%s", StateName(s_state), StateName(state),
        state == State::Shown ? " over the " : "", state == State::Shown ? GameScreens::AnchorName(s_anchor) : "");
    bool wasUp = s_state == State::Shown || s_state == State::Launching;
    bool up = state == State::Shown || state == State::Launching;
    if (wasUp && !up) {
        s_guard = true;
        s_guardTime = 0.0f;
    }
    s_state = state;
    s_stateTime = 0.0f;
    s_events.clear();
    s_repeat = {};
}

// The original's elements hidden (or shown again): the game's own hide_root_window / unhide_root_window (root_window
// scaled to 0 / 1) and root_window's alpha 0 / 1 at once - scaled to 0 alone, some elements still left marks: specks
// behind the restyled dialogs and a thin bar at the top centre of the pause screens (pause_menu_gradient's sprite
// squeezed to a line) - the user's report of 2026-10-25. An element's alpha is its parents' times its own, so nothing
// under root_window is drawn.
void HideRoot(bool hide, bool force = false)
{
    if (hide == s_rootHidden && !force)
        return;
    const char* script = hide ? "hide_root_window" : "unhide_root_window";
    if (!IsScript(Checksum(script)))
        return;
    Script::RunScript(Checksum(script), nullptr, nullptr, false, script);
    if (CScreenElement* root = GameScreens::Find(Checksum("root_window")))
        root->SetAlpha(hide ? 0.0f : 1.0f, 1);
    s_rootHidden = hide;
}

// While ours hides the original's elements, whatever script scales root_window back (create_pause_menu's
// unhide_root_window - a script making the pause menu again, the BMX's toggle, a dialog...) is undone at once: the
// original never shows under ours.
void KeepRootHidden()
{
    if (!s_rootHidden)
        return;
    CScreenElement* root = GameScreens::Find(Checksum("root_window"));
    if (!root || (root->m_target.scaleX < 0.001f && root->m_target.alpha < 0.001f))
        return;
    static int s_logged = 0;
    if (s_logged < 20) {
        ++s_logged;
        Log("new front end: the original's elements were shown again under ours (a script scaled root_window) - hidden");
    }
    HideRoot(true, true);
}

// --- phase 3: the original screens restyled (new_frontend_mirror.h) ---

uint32_t TypeOf(const CScreenElement* element) { return reinterpret_cast<const Obj::CObject*>(element)->m_type; }
uint32_t IdOfElement(const CScreenElement* element) { return reinterpret_cast<const Obj::CObject*>(element)->m_id; }
CScreenElement* FirstChild(const CScreenElement* element) { return element->mp_firstChild.Get(); }
CScreenElement* Next(const CScreenElement* element) { return element->mp_next.Get(); }
bool ElementHidden(const CScreenElement* element)
{
    return (reinterpret_cast<const Obj::CObject*>(element)->m_objectFlags & CScreenElement::kHidden) != 0;
}
bool Visible(const CScreenElement* element)
{
    return !ElementHidden(element) && element->m_target.alpha > 0.02f && element->m_target.rgba[3] > 2;
}

uint32_t TextType()
{
    static uint32_t type = Checksum("TextElement");
    return type;
}
uint32_t SpriteType()
{
    static uint32_t type = Checksum("SpriteElement");
    return type;
}

CScreenElement* RootElement()
{
    CScreenElementManager* manager = CScreenElementManager::Instance();
    return manager ? manager->mp_root.Get() : nullptr;
}

// The menu that takes the pad events now: of the focused menus, one in a dialog box first, else the last in the tree
// (the newest screen). `inDialog`: it is in dialog_box_container. `coming`: a menu not taking them yet counts too, when
// it is shown - the screen being made (the pause menu animates in for 13 frames before it takes the pad).
CScreenElement* ActiveMenu(bool* inDialog = nullptr, bool coming = false)
{
    CScreenElement* root = RootElement();
    if (!root)
        return nullptr;
    static const uint32_t kDialogContainer = Checksum("dialog_box_container");
    CScreenElement* found = nullptr;
    CScreenElement* dialog = nullptr;
    int budget = 8192;
    // (depth first, without recursion: the element, its first child, its next sibling, back up through the parents)
    struct Walk {
        CScreenElement* element;
        bool inDialog;
    };
    std::vector<Walk> stack;
    stack.push_back({FirstChild(root), false});
    while (!stack.empty() && --budget > 0) {
        Walk& top = stack.back();
        CScreenElement* e = top.element;
        if (!e) {
            stack.pop_back();
            continue;
        }
        bool here = top.inDialog || IdOfElement(e) == kDialogContainer;
        top.element = Next(e);
        if (MouseMenus::IsMenu(e) && (coming ? Visible(e) : MouseMenus::MenuGetsPadEvents(e))) {
            found = e;
            if (here)
                dialog = e;
        }
        if (CScreenElement* child = FirstChild(e))
            stack.push_back({child, here});
    }
    if (inDialog)
        *inDialog = dialog != nullptr;
    return dialog ? dialog : found;
}

// The strings of the text elements in `element` (itself included), in tree order; `skip`: subtrees left out.
void CollectTexts(const CScreenElement* element, std::vector<std::string>* out, const std::vector<const CScreenElement*>& skip,
                  int* budget, bool visibleOnly)
{
    if (!element || --*budget <= 0)
        return;
    if (std::find(skip.begin(), skip.end(), element) != skip.end())
        return;
    if (visibleOnly && !Visible(element))
        return;
    if (TypeOf(element) == TextType()) {
        const char* text = reinterpret_cast<const CTextElement*>(element)->m_string;
        if (text && *text)
            out->push_back(text);
    }
    for (CScreenElement* child = FirstChild(element); child && *budget > 0; child = Next(child))
        CollectTexts(child, out, skip, budget, visibleOnly);
}

// An element's height on the 640 x 480 layout: its position and its parents' added up (justification and scale aside -
// enough to tell rows apart).
float AbsoluteY(const CScreenElement* element)
{
    float y = 0.0f;
    for (int guard = 0; element && guard < 64; ++guard, element = element->mp_parent.Get())
        y += element->m_target.y;
    return y;
}

// The visible texts in `element` (itself included) with their heights, in tree order; `skip`: subtrees left out.
void CollectTextsAt(const CScreenElement* element, std::vector<std::pair<std::string, float>>* out,
                    const std::vector<const CScreenElement*>& skip, int* budget)
{
    if (!element || --*budget <= 0 || !Visible(element))
        return;
    if (std::find(skip.begin(), skip.end(), element) != skip.end())
        return;
    if (TypeOf(element) == TextType()) {
        const char* text = reinterpret_cast<const CTextElement*>(element)->m_string;
        if (text && *text)
            out->emplace_back(text, AbsoluteY(element));
    }
    for (CScreenElement* child = FirstChild(element); child && *budget > 0; child = Next(child))
        CollectTextsAt(child, out, skip, budget);
}

// A visible sprite in `element` that isn't a highlight bar (those are drawn at alpha 0 until their item is focused).
bool HasVisibleSprite(const CScreenElement* element, int* budget)
{
    if (!element || --*budget <= 0 || ElementHidden(element) || element->m_target.alpha <= 0.02f)
        return false;
    if (TypeOf(element) == SpriteType() && element->m_target.rgba[3] > 2 && std::fabs(element->m_target.scaleX) > 0.001f)
        return true;
    for (CScreenElement* child = FirstChild(element); child; child = Next(child))
        if (HasVisibleSprite(child, budget))
            return true;
    return false;
}

// An item's checkbox ("checkbox" sprite shown: ticked when its "checkmark" is) and whether its value changes with Left /
// Right (left_arrow / right_arrow sprites in it, shown on the focused item).
void ReadValueSprites(const CScreenElement* element, int* checkbox, bool* adjustable, int* budget)
{
    static const uint32_t kCheckbox = Checksum("checkbox"), kCheckmark = Checksum("checkmark");
    static const uint32_t kLeft = Checksum("left_arrow"), kRight = Checksum("right_arrow");
    for (const CScreenElement* e = element; e && --*budget > 0; e = Next(e)) {
        if (TypeOf(e) == SpriteType()) {
            uint32_t texture = reinterpret_cast<const CSpriteElement*>(e)->m_texture;
            if (texture == kLeft || texture == kRight)
                *adjustable = true;
            if (texture == kCheckbox && Visible(e)) {
                *checkbox = 0;
                for (const CScreenElement* mark = FirstChild(e); mark; mark = Next(mark))
                    if (TypeOf(mark) == SpriteType() && Visible(mark) &&
                        reinterpret_cast<const CSpriteElement*>(mark)->m_texture == kCheckmark)
                        *checkbox = 1;
            }
        }
        if (CScreenElement* child = FirstChild(e))
            ReadValueSprites(child, checkbox, adjustable, budget);
    }
}

Obj::CPlayerProfileManager* ProfileManager() { return Obj::g_profileManager; }

// The skater select's stats for the template `name`: stat_names' short_string (else string) and the template's value.
std::vector<std::pair<std::string, int>> SkaterStats(uint32_t name)
{
    std::vector<std::pair<std::string, int>> stats;
    Obj::CPlayerProfileManager* manager = ProfileManager();
    Obj::CSkaterProfile* profile = manager ? manager->GetTemplate(name) : nullptr;
    Script::CArray* names = Script::GetArray(Checksum("stat_names"));
    if (!profile || !names || names->m_type != Script::ESYMBOLTYPE_STRUCTURE)
        return stats;
    for (uint32_t i = 0; i < names->m_size && i < 16; ++i) {
        auto* entry = reinterpret_cast<Script::CStruct*>(names->GetElement(i));
        uint32_t stat = 0;
        const char* label = nullptr;
        if (!entry || !entry->GetChecksum(Checksum("name"), &stat, 0))
            continue;
        if (!entry->GetString(Checksum("short_string"), &label, 0))
            entry->GetString(Checksum("string"), &label, 0);
        stats.emplace_back(label ? label : "?", profile->GetStat(stat));
    }
    return stats;
}

Mirror::MenuRead ReadMenu(CScreenElement* menu, bool inDialog)
{
    Mirror::MenuRead read;
    if (!menu)
        return read;
    read.found = true;
    read.menuId = IdOfElement(menu);
    read.menuKey = reinterpret_cast<uintptr_t>(menu);
    read.vertical = MouseMenus::IsVerticalMenu(menu);
    read.selectedIndex = MouseMenus::SelectedIndex(menu);
    read.inDialog = inDialog;
    read.inLevel = !InFrontEnd();
    static const uint32_t kSkaterMenu = Checksum("select_skater_hmenu");
    bool skaters = read.menuId == kSkaterMenu;
    int budget = 4096;
    int index = 0;
    for (CScreenElement* child = FirstChild(menu); child && index < 80; child = Next(child), ++index) {
        Mirror::ItemRead item;
        item.id = IdOfElement(child);
        item.focusable = MouseMenus::ItemFocusable(child);
        CollectTexts(child, &item.texts, {}, &budget, false);
        if (!skaters && index != read.selectedIndex) {
            int spriteBudget = 256;
            item.graphicValue = HasVisibleSprite(child, &spriteBudget);
        }
        if (!skaters) {
            int valueBudget = 256;
            if (CScreenElement* first = FirstChild(child))
                ReadValueSprites(first, &item.checkbox, &item.adjustable, &valueBudget);
        }
        if (skaters) {
            Obj::CPlayerProfileManager* manager = ProfileManager();
            Obj::CSkaterProfile* profile = manager ? manager->GetTemplate(item.id) : nullptr;
            const char* name = profile ? profile->GetDisplayName() : nullptr;
            item.display = name ? name : "";
        }
        read.items.push_back(std::move(item));
    }
    if (inDialog) {
        // The title: the menu's first text; the text: the text block's lines (its children).
        static const uint32_t kTextBlock = Checksum("TextBlockElement");
        for (CScreenElement* child = FirstChild(menu); child; child = Next(child)) {
            if (read.dialogTitle.empty() && TypeOf(child) == TextType()) {
                const char* text = reinterpret_cast<CTextElement*>(child)->m_string;
                read.dialogTitle = text ? text : "";
            } else if (TypeOf(child) == kTextBlock) {
                std::vector<std::string> lines;
                CollectTexts(child, &lines, {}, &budget, false);
                std::string joined;
                for (const std::string& line : lines)
                    joined += (joined.empty() ? "" : " ") + line;
                read.dialogLines.push_back(joined);
            }
        }
        return read;
    }
    if (skaters) {
        if (CScreenElement* name = GameScreens::Find(Checksum("select_skater_name")))
            if (TypeOf(name) == TextType() && reinterpret_cast<CTextElement*>(name)->m_string)
                read.skaterName = reinterpret_cast<CTextElement*>(name)->m_string;
        if (read.selectedIndex >= 0 && read.selectedIndex < static_cast<int>(read.items.size()))
            read.stats = SkaterStats(read.items[read.selectedIndex].id);
        return read;
    }
    // A list: its title (title_text, the themed menus'), the other words on its screen (under the same child of the
    // root: not the menu, its title or the helper text).
    if (CScreenElement* title = GameScreens::Find(Checksum("title_text")))
        if (TypeOf(title) == TextType() && reinterpret_cast<CTextElement*>(title)->m_string && Visible(title))
            read.title = reinterpret_cast<CTextElement*>(title)->m_string;
    CScreenElement* root = RootElement();
    CScreenElement* screen = menu;
    for (int guard = 0; screen && guard < 64; ++guard) {
        CScreenElement* parent = screen->mp_parent.Get();
        if (!parent || parent == root)
            break;
        screen = parent;
    }
    std::vector<const CScreenElement*> skip = {menu};
    for (const char* id : {"helper_text_anchor", "submenu_title_anchor", "title_text"})
        if (CScreenElement* e = GameScreens::Find(Checksum(id)))
            skip.push_back(e);
    if (screen && screen != menu) {
        // A column beside the menu (the grab tricks' names beside their key combos): a word at a row's height is that
        // row's value; the rest are the screen's other words.
        std::vector<std::pair<std::string, float>> beside;
        CollectTextsAt(screen, &beside, skip, &budget);
        std::vector<float> rowY;
        for (CScreenElement* child = FirstChild(menu); child; child = Next(child)) {
            float y = AbsoluteY(child);
            for (CScreenElement* t = FirstChild(child); t; t = Next(t))
                if (TypeOf(t) == TextType()) {
                    y = AbsoluteY(t);
                    break;
                }
            rowY.push_back(y);
        }
        // (the nearest row, within half the rows' spacing)
        float spacing = 24.0f;
        for (size_t i = 1; i < rowY.size(); ++i)
            if (rowY[i] - rowY[i - 1] > 2.0f)
                spacing = std::min(spacing, rowY[i] - rowY[i - 1]);
        for (const auto& [text, y] : beside) {
            size_t row = rowY.size();
            float best = spacing * 0.5f;
            for (size_t i = 0; i < rowY.size() && i < read.items.size(); ++i)
                if (std::fabs(rowY[i] - y) < best && read.items[i].focusable) {
                    best = std::fabs(rowY[i] - y);
                    row = i;
                }
            if (row < rowY.size())
                read.items[row].texts.push_back(text);
            else
                read.extraTexts.push_back(text);
        }
    }
    return read;
}

// [frontend] debug_restyle=1: a menu left as it is (not restyled) logged once with its screen's element tree - what
// the reader would have to understand to restyle it (each element: type, id, text, texture, position, hidden).
const char* TypeName(uint32_t type)
{
    static const char* const kTypes[] = {"TextElement",     "SpriteElement", "ContainerElement", "VMenu",
                                         "HMenu",           "VScrollingMenu", "HScrollingMenu",  "TextBlockElement",
                                         "Element3d",       "WindowElement", "Menu"};
    for (const char* name : kTypes)
        if (Checksum(name) == type)
            return name;
    return nullptr;
}

void DumpTree(const CScreenElement* element, int depth, int* budget)
{
    for (; element && --*budget > 0; element = Next(element)) {
        char line[512];
        const char* type = TypeName(TypeOf(element));
        int at = std::snprintf(line, sizeof(line), "%*s%s %08X (%.0f, %.0f)%s", depth * 2, "", type ? type : "?",
                               IdOfElement(element), element->m_target.x, element->m_target.y,
                               Visible(element) ? "" : " hidden");
        if (TypeOf(element) == TextType()) {
            const char* text = reinterpret_cast<const CTextElement*>(element)->m_string;
            std::snprintf(line + at, sizeof(line) - at, " \"%.80s\"", text ? text : "");
        } else if (TypeOf(element) == SpriteType()) {
            std::snprintf(line + at, sizeof(line) - at, " texture %08X",
                          reinterpret_cast<const CSpriteElement*>(element)->m_texture);
        }
        Log("new front end: debug_restyle %s", line);
        if (depth < 8)
            DumpTree(FirstChild(element), depth + 1, budget);
    }
}

void DumpLeftMenu(CScreenElement* menu, const std::string& why)
{
    static const bool on = GetPrivateProfileIntA("frontend", "debug_restyle", 0, IniPath().c_str()) != 0;
    // (each menu once by what it holds: menus share ids and are rebuilt in the same element)
    static std::set<std::string> logged;
    std::vector<std::string> texts;
    int textBudget = 400;
    CollectTexts(menu, &texts, {}, &textBudget, false);
    std::string key = std::to_string(IdOfElement(menu));
    for (size_t i = 0; i < texts.size() && i < 6; ++i)
        key += "|" + texts[i];
    if (!on || !menu || !logged.insert(key).second)
        return;
    CScreenElement* root = RootElement();
    CScreenElement* screen = menu;
    for (int guard = 0; screen && guard < 64; ++guard) {
        CScreenElement* parent = screen->mp_parent.Get();
        if (!parent || parent == root)
            break;
        screen = parent;
    }
    Log("new front end: debug_restyle - the menu %08X (%s), its screen %08X:", IdOfElement(menu), why.c_str(),
        IdOfElement(screen));
    int budget = 600;
    const CScreenElement* one = screen;
    char line[160];
    std::snprintf(line, sizeof(line), "%s %08X", TypeName(TypeOf(one)) ? TypeName(TypeOf(one)) : "?", IdOfElement(one));
    Log("new front end: debug_restyle %s", line);
    DumpTree(FirstChild(screen), 1, &budget);
}

uint32_t VMenuType()
{
    static uint32_t type = Checksum("VMenu");
    return type;
}
uint32_t HMenuType()
{
    static uint32_t type = Checksum("HMenu");
    return type;
}

// The on-screen keyboard around `menu` (a row of keys - an HMenu - in a VMenu of such rows): the VMenu, else null.
CScreenElement* KeyboardOf(CScreenElement* menu)
{
    if (!menu || TypeOf(menu) != HMenuType())
        return nullptr;
    CScreenElement* parent = menu->mp_parent.Get();
    if (!parent || TypeOf(parent) != VMenuType())
        return nullptr;
    int rows = 0, singles = 0;
    for (CScreenElement* row = FirstChild(parent); row; row = Next(row)) {
        if (TypeOf(row) != HMenuType())
            continue;
        ++rows;
        for (CScreenElement* key = FirstChild(row); key; key = Next(key)) {
            std::vector<std::string> texts;
            int budget = 256;
            CollectTexts(key, &texts, {}, &budget, false);
            for (const std::string& t : texts)
                singles += t.size() == 1 ? 1 : 0;
        }
    }
    return rows >= 3 && singles >= 20 ? parent : nullptr;
}

// The keyboard read: its rows of keys (a key's words: the last that isn't a button tag, e.g. "Backspace"), the key
// selected, the text typed (the first row that isn't keys - its text, the cursor's "_" left in).
Mirror::MenuRead ReadKeyboard(CScreenElement* keys, CScreenElement* active)
{
    Mirror::MenuRead read;
    read.found = true;
    read.keyboard = true;
    read.menuId = IdOfElement(keys);
    read.menuKey = reinterpret_cast<uintptr_t>(keys);
    read.inLevel = !InFrontEnd();
    int row = 0;
    bool entryRead = false;
    for (CScreenElement* child = FirstChild(keys); child; child = Next(child)) {
        if (TypeOf(child) != HMenuType()) {
            if (!entryRead) {
                std::vector<std::string> texts;
                int budget = 32;
                CollectTexts(child, &texts, {}, &budget, false);
                if (!texts.empty())
                    read.entry = texts.front();
                entryRead = true;
            }
            continue;
        }
        std::vector<std::string> keyRow;
        for (CScreenElement* key = FirstChild(child); key; key = Next(key)) {
            std::vector<std::string> texts;
            int budget = 256; // (a wide key: a container of a score of cap sprites before its words)
            CollectTexts(key, &texts, {}, &budget, false);
            std::string label;
            for (const std::string& t : texts)
                if (!(t.size() >= 2 && t[0] == '\\'))
                    label = t;
            keyRow.push_back(label);
        }
        if (child == active) {
            read.keyRow = row;
            read.keyCol = MouseMenus::SelectedIndex(active);
        }
        read.keys.push_back(std::move(keyRow));
        ++row;
    }
    if (CScreenElement* title = GameScreens::Find(Checksum("title_text")))
        if (TypeOf(title) == TextType() && reinterpret_cast<CTextElement*>(title)->m_string && Visible(title))
            read.title = reinterpret_cast<CTextElement*>(title)->m_string;
    return read;
}

// A button tag as the game spells it (gfx/text.cpp's): the explicit buttons through the controller map when one is in
// use, the meta buttons through the meta map; the arrow glyphs Up / Down / Left / Right, else the key's name.
std::string ButtonName(char tag, char digit)
{
    int button;
    if (tag == 'm' || tag == 'M') {
        char d = digit;
        char mapped = Nx::ButtonGlyph(&d);
        button = Str::DehexifyDigit(&mapped);
    } else {
        uint8_t d = static_cast<uint8_t>(digit);
        button = static_cast<int>(Nx::ButtonForTag(&d));
    }
    switch (button) {
    case 4: return Text::kKeyDown;
    case 5: return Text::kKeyRight;
    case 6: return Text::kKeyLeft;
    case 7: return Text::kKeyUp;
    case 10: return Text::kKeyDownRight; // (the grab tricks' order: Down, Down-Left \bc, Down-Right \ba, ... Up-Left \bb,
    case 11: return Text::kKeyUpLeft;    // Up-Right \bd)
    case 12: return Text::kKeyDownLeft;
    case 13: return Text::kKeyUpRight;
    default: break;
    }
    const char* name = Sio::GetButtonKeyName(button);
    return name ? name : "";
}

Mirror::Tracker s_mirror;
std::vector<Paint::Hit> s_mirrorHits;
std::vector<SDL_Event> s_mirrorEvents;
float s_mirrorScroll = 0.0f;
// Set while the original's screens are those of a hand-off left to them entirely: the create modes' editors (CAS,
// CAT, CAG, the park editor, graphics) and online - nothing restyled until ours is up again.
const char* s_leftOriginal = nullptr;
std::string s_mirrorLogged;

bool DialogUp()
{
    bool inDialog = false;
    ActiveMenu(&inDialog);
    return inDialog;
}

Mirror::Policy MirrorPolicy()
{
    Mirror::Policy policy;
    std::string ini = IniPath();
    policy.lists = GetPrivateProfileIntA("frontend", "restyle_menus", 1, ini.c_str()) != 0;
    policy.dialogs = GetPrivateProfileIntA("frontend", "restyle_dialogs", 1, ini.c_str()) != 0;
    policy.skaters = GetPrivateProfileIntA("frontend", "restyle_skater_select", 1, ini.c_str()) != 0;
    policy.online = GetPrivateProfileIntA("frontend", "restyle_online", 1, ini.c_str()) != 0;
    std::string mode = GameScreens::GameModeName();
    policy.editor = s_leftOriginal != nullptr || mode == "parkeditor" || mode == "creategoals" ||
                    ScriptInteger("in_cat_preview_mode") == 1;
    return policy;
}

// What to restyle now (kind None: nothing).
Mirror::View MirrorNow()
{
    static Mirror::Policy policy;
    static int policyFrames = 0;
    if (policyFrames-- <= 0) { // (the ini read twice a second)
        policy = MirrorPolicy();
        policyFrames = 30;
        Mirror::SetButtonNamer(ButtonName);
    }
    // (online: restyled too unless [frontend] restyle_online=0 - tested by the user by hand, never by a tool)
    if (!LevelLoad() || LoadingScreenUp() || (GameScreens::NetSession() && !policy.online) || LevelEditor::Active())
        return {};
    bool inDialog = false;
    CScreenElement* menu = ActiveMenu(&inDialog);
    // (none takes the pad yet, paused or in the front end: the menu being made - restyled from its first frame, never
    // drawn as the game makes it while it animates in; half a second at most for each menu - one that still doesn't
    // take the pad belongs to a screen worked some other way, left as the game made it)
    static CScreenElement* s_coming = nullptr;
    static int s_comingFrames = 0;
    constexpr int kComingAtMost = 30;
    if (!menu && (Paused() || InFrontEnd())) {
        CScreenElement* coming = ActiveMenu(&inDialog, true);
        s_comingFrames = coming && coming == s_coming ? s_comingFrames + 1 : 1;
        s_coming = coming;
        if (s_comingFrames <= kComingAtMost)
            menu = coming;
    } else {
        s_coming = nullptr;
    }
    if (!menu)
        return {};
    // A create mode's editor chosen on the restyled CREATE-A-MODES (its item selected when that menu went): its menus
    // are left as they are - until the main menu or CREATE-A-MODES is back.
    static uint32_t s_lastMenu = 0, s_lastItem = 0;
    static const char* const kEditor = "a create mode's editor";
    uint32_t menuId = IdOfElement(menu);
    bool createModes = GameScreens::Find(Checksum("menu_cas")) != nullptr;
    if (s_leftOriginal == kEditor && (menuId == Checksum("main_vmenu") || createModes))
        s_leftOriginal = nullptr;
    if (!s_leftOriginal && menuId != s_lastMenu && s_lastMenu == Checksum("main_menu") && !createModes &&
        menuId != Checksum("main_vmenu")) { // (Back to the main menu isn't an editor)
        for (const char* editor : {"menu_cas", "menu_cat", "menu_creategoals", "menu_parked", "menu_cagr"})
            if (s_lastItem == Checksum(editor)) {
                Log("new front end: %s - its menus left as they are", editor);
                s_leftOriginal = kEditor;
            }
    }
    s_lastMenu = menuId;
    {
        int selected = MouseMenus::SelectedIndex(menu);
        CScreenElement* item = FirstChild(menu);
        for (int i = 0; item && i < selected; ++i)
            item = Next(item);
        s_lastItem = item && selected >= 0 ? IdOfElement(item) : 0;
    }
    Mirror::Policy now = policy;
    now.editor = now.editor || s_leftOriginal != nullptr;
    CScreenElement* keyboard = inDialog ? nullptr : KeyboardOf(menu);
    Mirror::View view = Mirror::Build(keyboard ? ReadKeyboard(keyboard, menu) : ReadMenu(menu, inDialog), now);
    // (each menu's decision logged once)
    char key[96];
    std::snprintf(key, sizeof(key), "%08X %s %s", IdOfElement(menu), Mirror::KindName(view.kind), view.why.c_str());
    if (s_mirrorLogged != key) {
        s_mirrorLogged = key;
        if (view.kind != Mirror::Kind::None)
            Log("new front end: restyled - the original's %s (%08X) \"%s\", %zu rows", Mirror::KindName(view.kind),
                IdOfElement(menu), view.title.c_str(), view.rows.size());
        else
            Log("new front end: the original's menu %08X left as it is: %s", IdOfElement(menu),
                view.why.empty() ? "not restyled" : view.why.c_str());
    }
    if (view.kind == Mirror::Kind::None)
        DumpLeftMenu(menu, view.why);
    return view;
}

// The mouse over the restyled screen: the original menu's selection stepped to the row (its own pad events), a click
// chooses it, the right button / Back is pad_back, the wheel steps.
struct StepContext {
    Mdl::FrontEnd* frontEnd;
    bool horizontal;
};
int ReadSelection(void*)
{
    CScreenElement* menu = ActiveMenu();
    return menu ? MouseMenus::SelectedIndex(menu) : -1;
}
void SendStep(void* context, int direction)
{
    auto* step = static_cast<StepContext*>(context);
    uint32_t event = step->horizontal ? (direction < 0 ? Mdl::FrontEnd::kPadLeft : Mdl::FrontEnd::kPadRight)
                                      : (direction < 0 ? Mdl::FrontEnd::kPadUp : Mdl::FrontEnd::kPadDown);
    MouseMenus::SendMenuPadEvent(step->frontEnd, event);
}
bool StepTo(Mdl::FrontEnd* frontEnd, int row)
{
    const Mirror::View& v = s_mirror.Shown();
    CScreenElement* menu = ActiveMenu();
    if (!menu || row < 0 || row >= static_cast<int>(v.rows.size()))
        return false;
    int target = v.rows[row].index;
    StepContext context{frontEnd, v.horizontal};
    MouseMenus::StepSelection(target, MouseMenus::ChildCount(menu), MouseMenus::MenuWraps(menu), ReadSelection, SendStep,
                              &context);
    return ReadSelection(nullptr) == target;
}

// The keyboard's key (row, col) selected with the game's own pad events (each step read back), then chosen.
void ChooseKey(Mdl::FrontEnd* frontEnd, int row, int col)
{
    for (int guard = 0; guard < 64; ++guard) {
        CScreenElement* active = ActiveMenu();
        CScreenElement* keys = KeyboardOf(active);
        if (!keys)
            return;
        Mirror::MenuRead now = ReadKeyboard(keys, active);
        if (now.keyRow != row) {
            MouseMenus::SendMenuPadEvent(frontEnd, now.keyRow < row ? Mdl::FrontEnd::kPadDown : Mdl::FrontEnd::kPadUp);
            continue;
        }
        if (now.keyCol != col) {
            MouseMenus::SendMenuPadEvent(frontEnd, now.keyCol < col ? Mdl::FrontEnd::kPadRight : Mdl::FrontEnd::kPadLeft);
            continue;
        }
        MouseMenus::SendMenuPadEvent(frontEnd, Mdl::FrontEnd::kPadChoose);
        return;
    }
}

void MirrorMouse(Mdl::FrontEnd* frontEnd)
{
    std::vector<SDL_Event> events;
    events.swap(s_mirrorEvents);
    if (!s_mirror.Up())
        return;
    int w = 0, h = 0;
    if (SDL_Window* window = Sys::SdlWindow())
        SDL_GetWindowSize(window, &w, &h);
    Frame frame = FitFrame(static_cast<float>(w), static_cast<float>(h));
    auto hitAt = [&frame](float wx, float wy) -> const Paint::Hit* {
        float x = frame.DesignX(wx), y = frame.DesignY(wy);
        for (const Paint::Hit& hit : s_mirrorHits)
            if (hit.Contains(x, y))
                return &hit;
        return nullptr;
    };
    bool skaters = s_mirror.Shown().kind == Mirror::Kind::Skaters;
    for (const SDL_Event& e : events) {
        switch (e.type) {
        case SDL_MOUSEMOTION:
            // (the skater select: each step loads a skater - only a click selects there)
            if (!skaters)
                if (const Paint::Hit* hit = hitAt(static_cast<float>(e.motion.x), static_cast<float>(e.motion.y)))
                    if (hit->kind == Paint::Hit::Item && hit->index != s_mirror.Shown().selected)
                        StepTo(frontEnd, hit->index);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_RIGHT) {
                MouseMenus::SendMenuPadEvent(frontEnd, Mdl::FrontEnd::kPadBack);
            } else if (e.button.button == SDL_BUTTON_LEFT) {
                const Paint::Hit* hit = hitAt(static_cast<float>(e.button.x), static_cast<float>(e.button.y));
                if (!hit)
                    break;
                if (hit->kind == Paint::Hit::Back) {
                    MouseMenus::SendMenuPadEvent(frontEnd, Mdl::FrontEnd::kPadBack);
                } else if (hit->kind == Paint::Hit::Level && s_mirror.Shown().kind == Mirror::Kind::Keyboard) {
                    ChooseKey(frontEnd, hit->index / 64, hit->index % 64);
                } else if (hit->kind == Paint::Hit::Continue) {
                    ContinueLastLevel(frontEnd);
                } else if (hit->kind == Paint::Hit::Item) {
                    bool wasSelected = hit->index == s_mirror.Shown().selected;
                    if (StepTo(frontEnd, hit->index) && (!skaters || wasSelected))
                        MouseMenus::SendMenuPadEvent(frontEnd, Mdl::FrontEnd::kPadChoose);
                }
                return; // (the screen may be another now)
            }
            break;
        case SDL_MOUSEWHEEL:
            if (e.wheel.y) {
                StepContext context{frontEnd, s_mirror.Shown().horizontal};
                SendStep(&context, e.wheel.y > 0 ? -1 : 1);
            }
            break;
        // (the main menu's Continue - ours: the game's menu has no such item; every other key the game's menu takes)
        case SDL_KEYDOWN:
            if (s_mirror.Shown().mainMenu && !e.key.repeat &&
                (e.key.keysym.scancode == SDL_SCANCODE_X || e.key.keysym.scancode == SDL_SCANCODE_R)) {
                ContinueLastLevel(frontEnd);
                return;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (s_mirror.Shown().mainMenu && e.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
                ContinueLastLevel(frontEnd);
                return;
            }
            break;
        default: break;
        }
    }
}

// The cover over a hand-off's end (Cover in new_frontend_model.h): what it draws meanwhile - ours (s_menu as it was)
// or the restyled screen last up (a copy: the tracker forgets it as it goes).
Cover s_cover;
Mirror::Tracker s_coverTracker;
float s_coverScroll = 0.0f;
bool s_mirrorWouldRestyle = false; // (this frame's decision: the menu up is one to restyle)
// The original's elements held hidden though nothing is restyled yet: frames left while a menu that would be (or is
// still being built - View::unsettled) went; frames a menu has stayed unsettled (shown as the game made it after
// kFramesUnsettled).
int s_holdFrames = 0;
int s_unsettledFrames = 0;
// The front end come up (loaded, the loading screen gone) and none of its menus restyled or left original yet: ours
// draws its ground and the main menu's art meanwhile (Paint::Waiting) - the front end's 3D scene never shows bare.
bool s_frontEndWas = false;
bool s_awaitingFrontEnd = false;
float s_awaitTime = 0.0f;
constexpr float kAwaitAtMost = 8.0f; // (seconds: then the front end as it is)

void StartCover(Cover::Over over, const char* why)
{
    if (s_cover.On() && s_cover.Drawn() == over)
        return;
    Log("new front end: covering - %s (%s drawn until the next screen is ready)", why,
        over == Cover::Over::Ours ? "ours" : "the restyled screen");
    s_cover.Start(over);
    HideRoot(true);
}

// Once a frame while ours isn't drawn (BeforeScreenLayout): the restyled screen up or down, the original's elements
// hidden under it.
void UpdateMirror(float dt, bool allowed)
{
    Mirror::View now = allowed ? MirrorNow() : Mirror::View{};
    s_mirrorWouldRestyle = now.kind != Mirror::Kind::None;
    // (a menu that will be restyled, or one still being built: its elements hidden from the frame it is first seen -
    // the tracker brings ours up a frame or so later; meanwhile nothing of the menu is drawn, never the original)
    s_unsettledFrames = now.kind == Mirror::Kind::None && now.unsettled ? s_unsettledFrames + 1 : 0;
    if (s_mirrorWouldRestyle || (s_unsettledFrames > 0 && s_unsettledFrames <= Mirror::kFramesUnsettled))
        s_holdFrames = Mirror::kFramesToHide;
    else if (s_holdFrames > 0)
        --s_holdFrames;
    bool wasUp = s_mirror.Up();
    Mirror::Tracker before;
    if (wasUp)
        before = s_mirror;
    bool up = s_mirror.Update(now, dt);
    if (up && !wasUp) {
        Log("new front end: restyling the original's %s", Mirror::KindName(s_mirror.Shown().kind));
        s_mirrorScroll = 0.0f;
    }
    if (!up && wasUp) {
        Log("new front end: the restyled screen gone");
        s_mirrorHits.clear();
        // (what comes next - ours, another restyled screen, an original one - may take a few frames: the restyled
        // screen stays drawn meanwhile, never the original under it)
        if (allowed && LevelLoad() && !LoadingScreenUp()) {
            s_coverTracker = before;
            s_coverScroll = s_mirrorScroll;
            StartCover(Cover::Over::Restyled, "the restyled screen went");
        }
    }
    if (up || s_holdFrames > 0)
        HideRoot(true);
}

// Once a frame while a cover is on (and ours isn't up): off once the screen the hand-off went to is ready. When it ends
// with nothing restyled, the original's elements come back the same frame (BeforeScreenLayout).
void UpdateCover(float dt)
{
    if (!s_cover.On())
        return;
    bool inDialog = false;
    CScreenElement* active = ActiveMenu(&inDialog);
    Cover::Now now;
    now.targetReady = s_mirror.Up();
    now.gameplay = !LevelLoad() || LoadingScreenUp() || LevelEditor::Active() || (!InFrontEnd() && !Paused() && !active);
    now.screen = reinterpret_cast<uintptr_t>(active);
    now.leftOriginal = active && !s_mirrorWouldRestyle;
    const char* why = nullptr;
    if (!s_cover.Update(now, dt, &why))
        Log("new front end: cover off - %s (%.2f s)", why ? why : "?", s_cover.Time());
}

void FreeDeviceObjects();

// Ours up: the screen a menu's item asked for (NewFrontend_Open), on its root - Main in the front end, Pause in a level
// (never shown: back at it, ours goes aside). `anchor`: the original screen up under it (or None).
void Show(Anchor anchor)
{
    LoadCatalogue();
    if (s_openScreen == Screen::Main) {
        SetState(State::Away);
        return;
    }
    Screen root = InFrontEnd() ? Screen::Main : Screen::Pause;
    std::vector<Screen> stack = {root, s_openScreen};
    LevelsFor levels = s_openLevels;
    s_openRoot = root;
    s_shownBack = s_openBack;
    s_openScreen = Screen::Main;
    s_openBack = 0;
    HideRoot(true);
    s_anchor = anchor;
    s_leftOriginal = nullptr;
    if (s_cover.On())
        Log("new front end: cover off - ours up");
    s_cover.Stop();
    if (s_mirror.Up())
        Log("new front end: the restyled screen gone (ours up)");
    s_mirror.Reset();
    s_mirrorHits.clear();
    s_menu.Reset(&s_catalogue, Views(), s_saved.lastLevel, stack, levels);
    LoadOptionValues();
    s_skaterName = root == Screen::Pause ? std::string() : SkaterName();
    s_hits.clear();
    SetState(State::Shown);
}

void Do(Mdl::FrontEnd* frontEnd, const Action& action)
{
    (void)frontEnd;
    switch (action.kind) {
    case ActionKind::None: return;
    case ActionKind::MoveSound: Sound("generic_menu_up_or_down_sound", "up"); return;
    case ActionKind::ChooseSound: Sound("generic_menu_pad_choose_sound"); return;
    case ActionKind::BackSound: Sound("generic_menu_pad_back_sound"); return;
    case ActionKind::GameOptions: {
        Sound("generic_menu_pad_choose_sound");
        {
            // (GAME OPTIONS as the pause menu's / main menu's own item opens it - its script, the screen it makes
            // restyled; ours aside)
            const char* script = InFrontEnd() ? "create_setup_options_menu" : "create_options_menu";
            Log("new front end: the game options (%s)", script);
            SetState(State::Away);
            s_shownBack = 0;
            Script::SpawnScript(Checksum(script), nullptr, 0, nullptr, 0, 0, false, false, false, false);
            return;
        }
    }
    case ActionKind::Option: ApplyOption(action.option, action.value); Sound("generic_menu_up_or_down_sound", "up"); return;
    case ActionKind::Skate: {
        std::string why;
        if (action.levels == LevelsFor::Pause) {
            // (the pause menu's change of level, as its level select does it)
            Log("new front end: change level to %s", action.load.c_str());
            HideRoot(false);
            Script::CStruct* params = Script::NewStruct()->Init();
            params->AddChecksum(Checksum("level"), Checksum(action.load.c_str()));
            Script::SpawnScript(Checksum("level_select_change_level"), params, 0, nullptr, 0, 0, false, false, false, false);
            params->Clear();
            Script::DeleteStruct(params);
            s_launchLevel = LevelLoad();
            SetState(State::Launching);
            return;
        }
        if (StartLevel::Load(action.load.c_str(), &why)) {
            s_saved.lastLevel = action.load;
            s_menu.SetLastLevel(action.load);
            SaveState();
            s_launchLevel = LevelLoad();
            if (action.levels == LevelsFor::Edit) {
                s_edit = Edit::WaitLevel;
                s_editTime = 0.0f;
                s_editFrames = s_editPresses = 0;
                Log("new front end: the level editor opens once %s is up", action.load.c_str());
            }
            SetState(State::Launching);
        } else {
            Log("new front end: %s didn't start: %s", action.load.c_str(), why.c_str());
            s_menu.CancelLaunch();
        }
        return;
    }
    }
}

// --- input ---

bool KeyDown(SDL_Scancode key) { return SDL_GetKeyboardState(nullptr)[key] != 0; }

void PressDirection(Mdl::FrontEnd* frontEnd, Input input) { Do(frontEnd, s_menu.Press(input)); }

void HandleQueued(Mdl::FrontEnd* frontEnd, const SDL_Event& e)
{
    int w = 0, h = 0;
    if (SDL_Window* window = Sys::SdlWindow())
        SDL_GetWindowSize(window, &w, &h);
    Frame frame = FitFrame(static_cast<float>(w), static_cast<float>(h));
    auto hitAt = [&frame](float wx, float wy) -> const Paint::Hit* {
        float x = frame.DesignX(wx), y = frame.DesignY(wy);
        for (const Paint::Hit& hit : s_hits)
            if (hit.Contains(x, y))
                return &hit;
        return nullptr;
    };
    switch (e.type) {
    case SDL_KEYDOWN: {
        bool repeat = e.key.repeat != 0;
        bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
        switch (e.key.keysym.scancode) {
        case SDL_SCANCODE_UP: case SDL_SCANCODE_W: PressDirection(frontEnd, Input::Up); break;
        case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: PressDirection(frontEnd, Input::Down); break;
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: PressDirection(frontEnd, Input::Left); break;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: PressDirection(frontEnd, Input::Right); break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
            if (!repeat)
                Do(frontEnd, s_menu.Press(Input::Confirm));
            break;
        case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE:
            if (!repeat)
                Do(frontEnd, s_menu.Press(Input::Back));
            break;
        case SDL_SCANCODE_Q: case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_1:
            Do(frontEnd, s_menu.Press(Input::PrevTab));
            break;
        case SDL_SCANCODE_E: case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_2:
            Do(frontEnd, s_menu.Press(Input::NextTab));
            break;
        case SDL_SCANCODE_TAB: Do(frontEnd, s_menu.Press(shift ? Input::PrevTab : Input::NextTab)); break;
        default: break;
        }
        return;
    }
    case SDL_CONTROLLERBUTTONDOWN:
        switch (e.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: PressDirection(frontEnd, Input::Up); s_repeat = {static_cast<int>(Input::Up), kRepeatDelay}; break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: PressDirection(frontEnd, Input::Down); s_repeat = {static_cast<int>(Input::Down), kRepeatDelay}; break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: PressDirection(frontEnd, Input::Left); s_repeat = {static_cast<int>(Input::Left), kRepeatDelay}; break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: PressDirection(frontEnd, Input::Right); s_repeat = {static_cast<int>(Input::Right), kRepeatDelay}; break;
        case SDL_CONTROLLER_BUTTON_A: Do(frontEnd, s_menu.Press(Input::Confirm)); break;
        case SDL_CONTROLLER_BUTTON_START:
            // (Start on the pause menu resumes, as the original's; elsewhere it chooses)
            Do(frontEnd, s_menu.Press(s_menu.Shown() == Screen::Pause && s_menu.Depth() <= 1 ? Input::Back : Input::Confirm));
            break;
        case SDL_CONTROLLER_BUTTON_B: Do(frontEnd, s_menu.Press(Input::Back)); break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: Do(frontEnd, s_menu.Press(Input::PrevTab)); break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: Do(frontEnd, s_menu.Press(Input::NextTab)); break;
        default: break;
        }
        return;
    case SDL_CONTROLLERBUTTONUP:
        if (e.cbutton.button >= SDL_CONTROLLER_BUTTON_DPAD_UP && e.cbutton.button <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
            s_repeat = {};
        return;
    case SDL_CONTROLLERAXISMOTION: {
        // The left stick as a d-pad: past half way (back inside a third: let go).
        int* axis = e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ? &s_stickX : e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY ? &s_stickY : nullptr;
        if (!axis)
            return;
        int value = e.caxis.value;
        int now = value > 16000 ? 1 : value < -16000 ? -1 : (std::abs(value) < 11000 ? 0 : *axis);
        if (now == *axis)
            return;
        *axis = now;
        if (now == 0) {
            s_repeat = {};
            return;
        }
        Input input = axis == &s_stickX ? (now > 0 ? Input::Right : Input::Left) : (now > 0 ? Input::Down : Input::Up);
        PressDirection(frontEnd, input);
        s_repeat = {static_cast<int>(input), kRepeatDelay};
        return;
    }
    case SDL_MOUSEMOTION:
        if (const Paint::Hit* hit = hitAt(static_cast<float>(e.motion.x), static_cast<float>(e.motion.y))) {
            if (hit->kind == Paint::Hit::Item)
                Do(frontEnd, s_menu.HoverItem(hit->index));
            else if (hit->kind == Paint::Hit::Level)
                Do(frontEnd, s_menu.HoverLevel(hit->index));
        }
        return;
    case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_RIGHT) {
            Do(frontEnd, s_menu.Press(Input::Back));
            return;
        }
        if (e.button.button != SDL_BUTTON_LEFT)
            return;
        if (const Paint::Hit* hit = hitAt(static_cast<float>(e.button.x), static_cast<float>(e.button.y))) {
            Paint::Hit clicked = *hit;
            switch (clicked.kind) {
            case Paint::Hit::Item: Do(frontEnd, s_menu.ClickItem(clicked.index)); break;
            case Paint::Hit::Level: Do(frontEnd, s_menu.ClickLevel(clicked.index)); break;
            case Paint::Hit::Tab: Do(frontEnd, s_menu.ClickTab(clicked.index)); break;
            case Paint::Hit::Back: Do(frontEnd, s_menu.ClickBack()); break;
            case Paint::Hit::Skate:
                Do(frontEnd, s_menu.Shown() == Screen::Options ? s_menu.Press(Input::Confirm) : s_menu.ClickSkate());
                break;
            case Paint::Hit::Continue: break; // (the restyled main menu's - MirrorMouse)
            case Paint::Hit::Choice: Do(frontEnd, s_menu.ClickChoice(clicked.index / 16, clicked.index % 16)); break;
            }
        }
        return;
    case SDL_MOUSEWHEEL:
        if (e.wheel.y)
            Do(frontEnd, s_menu.Wheel(e.wheel.y));
        return;
    default: return;
    }
}

void HandleEvent(SDL_Event* event)
{
    if (!Enabled())
        return;
    switch (event->type) {
    case SDL_CONTROLLERBUTTONDOWN: ++s_buttonsDown; break;
    case SDL_CONTROLLERBUTTONUP: s_buttonsDown = std::max(0, s_buttonsDown - 1); break;
    default: break;
    }
    if (s_state != State::Shown) {
        // (a restyled original screen: the mouse is ours - its keys and buttons go on to the original menu)
        if (s_mirror.Up() && s_mirrorEvents.size() < 64 &&
            (event->type == SDL_MOUSEMOTION || event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEWHEEL ||
             event->type == SDL_KEYDOWN || event->type == SDL_CONTROLLERBUTTONDOWN))
            s_mirrorEvents.push_back(*event);
        return;
    }
    switch (event->type) {
    case SDL_KEYDOWN:
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    case SDL_CONTROLLERAXISMOTION:
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEWHEEL:
        if (s_events.size() < 256)
            s_events.push_back(*event);
        break;
    default: break;
    }
}

// --- the Level Editor from the Create screen ---

void EditUpdate(Mdl::FrontEnd* frontEnd, float dt)
{
    if (s_edit == Edit::Off)
        return;
    s_editTime += dt;
    if (s_editTime > 60.0f) {
        Log("new front end: the level editor didn't open in 60 s - free skate as it is");
        s_edit = Edit::Off;
        return;
    }
    bool inLevel = LevelLoad() && !InFrontEnd() && Obj::FindObject(kSkater) && !LoadingScreenUp();
    if (!inLevel) {
        s_editFrames = 0;
        return;
    }
    if (s_edit == Edit::WaitLevel) {
        s_editFrames = Paused() ? 0 : s_editFrames + 1;
        if (s_editFrames < 90)
            return;
        // Start, as the player presses it: handle_start_pressed pauses and makes the pause menu.
        MouseMenus::SendMenuPadEvent(frontEnd, Mdl::FrontEnd::kPadStart);
        ++s_editPresses;
        s_edit = Edit::WaitPause;
        s_editFrames = 0;
        return;
    }
    // (WaitPause) the pause menu up with its LEVEL EDITOR: chosen.
    if (!Paused()) {
        if (++s_editFrames >= 120 && s_editPresses < 3) {
            MouseMenus::SendMenuPadEvent(frontEnd, Mdl::FrontEnd::kPadStart);
            ++s_editPresses;
            s_editFrames = 0;
        }
        return;
    }
    if (++s_editFrames < 20)
        return;
    s_edit = Edit::Off;
    if (CScreenElement* item = GameScreens::Find(Checksum("menu_level_editor"))) {
        Log("new front end: the pause menu's LEVEL EDITOR chosen");
        GameScreens::LaunchPad(frontEnd, Mdl::FrontEnd::kPadChoose, item);
    } else {
        Log("new front end: no LEVEL EDITOR in the pause menu (refused here?) - the pause menu as it is");
    }
}

// --- the ImGui context ---

ImGuiContext* s_context = nullptr;
bool s_contextFailed = false;
Paint::FontFiles s_fonts;
bool s_fontsResolved = false;

bool BeginContext(ImGuiContext** previous)
{
    *previous = ImGui::GetCurrentContext();
    if (!s_context) {
        if (s_contextFailed)
            return false;
        s_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(s_context);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        // (the cursor is the game's - MouseMenus shows it in the front end; ImGui leaves it alone)
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
        if (!UiBackend::Init()) {
            ImGui::DestroyContext(s_context);
            s_context = nullptr;
            s_contextFailed = true;
            ImGui::SetCurrentContext(*previous);
            Log("new front end: ImGui couldn't start on the game's window / device - not drawn");
            return false;
        }
    }
    ImGui::SetCurrentContext(s_context);
    return true;
}

void EndContext(ImGuiContext* previous) { ImGui::SetCurrentContext(previous); }

void FreeDeviceObjects()
{
    FreeArt();
    if (!s_context)
        return;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(s_context);
    UiBackend::InvalidateContextObjects();
    ImGui::SetCurrentContext(previous);
}

// --- the automation channel's commands ---

struct Pending {
    bool active = false;
    bool confirm = false;
    Screen screen = Screen::Main;
    std::string item;
    std::string result; // (set once done)
};
Pending s_pending;

bool ShownAndIdle(Screen screen) { return s_state == State::Shown && !s_menu.Busy() && s_menu.Shown() == screen; }

// The things the channel never does through ours: anything online (none of ours goes online now - the game's ONLINE
// item is the game's; kept for the commands' shape).
bool Refused(Screen screen, const std::string& item)
{
    (void)screen;
    (void)item;
    return false;
}

} // namespace

bool& Enabled()
{
    static bool enabled = ConfigInt("features", "new_frontend", 0) != 0;
    return enabled;
}

void Init()
{
    if (!Enabled())
        return;
    Platform::RegisterEventHandler(HandleEvent);
    Log("new front end: on ([features] new_frontend=1)");
}

bool Showing() { return s_state == State::Shown || s_state == State::Launching; }

// (while a cover is on, the screen under it is hidden: no presses reach it blind - a few frames, 3 s at most)
bool BlocksGameInput() { return Enabled() && (Showing() || s_guard || s_cover.On()); }

void Update(Mdl::FrontEnd* frontEnd)
{
    if (!Enabled())
        return;
    auto now = std::chrono::steady_clock::now();
    float dt = s_ticked ? std::chrono::duration<float>(now - s_lastTick).count() : 0.0f;
    s_lastTick = now;
    s_ticked = true;
    dt = std::clamp(dt, 0.0f, 0.25f);
    s_stateTime += dt;
    LoadCatalogue();
    NoteLevelLoaded();
    UpdateSession(dt);
    EditUpdate(frontEnd, dt);

    if (s_guard) {
        s_guardTime += dt;
        bool held = KeyDown(SDL_SCANCODE_RETURN) || KeyDown(SDL_SCANCODE_KP_ENTER) || KeyDown(SDL_SCANCODE_SPACE) ||
                    KeyDown(SDL_SCANCODE_ESCAPE) || KeyDown(SDL_SCANCODE_BACKSPACE) || s_buttonsDown > 0 ||
                    (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK);
        if (!held || s_guardTime > 2.0f)
            s_guard = false;
    }

    // A tool's own UI has the screen (the level editor - its Dear ImGui screens, or its old HUD of screen elements):
    // ours steps aside completely - not drawn, nothing restyled, nothing covered, the original's elements as the tool
    // left them - and comes back only once the pause menu really is back (the user's report of 2026-10-25: our pause
    // menu stayed drawn over the level editor - its hand-off never finished, as this isn't called while the editor
    // runs... MouseMenus::Update calls it then too, for this).
    if (LevelEditor::Active()) {
        if (s_state != State::Idle && s_state != State::Away) {
            Log("new front end: the level editor is up - ours aside");
            SetState(State::Away);
        }
        if (s_cover.On())
            Log("new front end: cover off - the level editor is up");
        s_cover.Stop();
        if (s_mirror.Up()) {
            s_mirror.Reset();
            s_mirrorHits.clear();
        }
        s_events.clear();
        s_mirrorEvents.clear();
        HideRoot(false);
        return;
    }

    Anchor anchor = GameScreens::Current(frontEnd);
    // The menu the pad reaches now: a dialog box over an anchor, or another menu over the pause menu (a sub-screen that
    // keeps pause_vmenu - VIEW GOALS, the level select...), and ours isn't over it (the dialog / screen is restyled, or
    // left as it is).
    bool inDialog = false;
    CScreenElement* active = ActiveMenu(&inDialog);
    bool pauseActive = !active || IdOfElement(active) == Checksum("pause_vmenu");
    (void)pauseActive;
    // (every menu is the game's, restyled - ours only for a screen a menu's item asked for, while that is up)
    bool usable = LevelLoad() && !LoadingScreenUp() && !inDialog && s_edit == Edit::Off &&
                  (s_openScreen != Screen::Main || s_state == State::Shown);

    switch (s_state) {
    case State::Idle:
    case State::Away:
        if (!LevelLoad() || (!InFrontEnd() && anchor == Anchor::None && s_state == State::Away && !Paused())) {
            if (s_state != State::Idle) {
                SetState(State::Idle);
                FreeDeviceObjects();
            }
        }
        if (usable) {
            Show(anchor);
            KeepRootHidden();
            return;
        }
        // (the restyled screen and the cover are kept by BeforeScreenLayout; the mouse over the restyled screen here)
        if (s_mirror.Up())
            MirrorMouse(frontEnd);
        else
            s_mirrorEvents.clear();
        KeepRootHidden();
        return;
    case State::Launching:
        s_menu.Tick(dt);
        if ((s_launchLevel == kLoadMainMenu && !InFrontEnd()) || (s_launchLevel != kLoadMainMenu && LevelLoad() != s_launchLevel) ||
            (s_launchLevel != kLoadMainMenu && LoadingScreenUp())) {
            SetState(State::Idle);
            FreeDeviceObjects();
        } else if (s_stateTime > 30.0f) {
            Log("new front end: the level didn't start in 30 seconds - back to the menus");
            s_menu.CancelLaunch();
            SetState(State::Idle);
        }
        return;
    case State::Shown:
        break;
    }
    KeepRootHidden();
    if (s_menu.Shown() == s_openRoot && !s_menu.Busy()) {
        // (back from the screen a menu's item asked for: that menu - the game's, restyled - again, or the script it
        // gave for Back when its menu went - the skate shop's)
        Log("new front end: back from our screen%s", s_shownBack ? " - its back script" : "");
        SetState(State::Away);
        if (s_shownBack)
            Script::SpawnScript(s_shownBack, nullptr, 0, nullptr, 0, 0, false, false, false, false);
        s_shownBack = 0;
        return;
    }
    if (!usable) {
        // (the original's screen changed on its own: ours aside - back when an anchor is up)
        if (anchor == Anchor::None && !InFrontEnd() && !Paused()) {
            SetState(State::Idle);
            FreeDeviceObjects();
        } else if (usable && anchor != Anchor::None) {
            Show(anchor);
        } else {
            SetState(State::Away);
            // (a dialog over the anchor, a sub-screen over the pause menu...: ours stays on screen until it is ready)
            if (LevelLoad() && !LoadingScreenUp())
                StartCover(Cover::Over::Ours, "the original's screen changed under ours");
        }
        return;
    }
    std::vector<SDL_Event> events;
    events.swap(s_events);
    for (const SDL_Event& e : events) {
        HandleQueued(frontEnd, e);
        if (s_state != State::Shown)
            return;
    }
    if (s_repeat.direction >= 0) {
        s_repeat.wait -= dt;
        if (s_repeat.wait <= 0.0f) {
            s_repeat.wait += kRepeatEvery;
            PressDirection(frontEnd, static_cast<Input>(s_repeat.direction));
        }
    }
    // The automation channel's command waiting for its screen.
    if (s_pending.active && ShownAndIdle(s_pending.screen)) {
        s_pending.active = false;
        if (s_menu.SelectId(s_pending.item)) {
            Log("new front end: automation - %s/%s %s", ScreenName(s_pending.screen), s_pending.item.c_str(),
                s_pending.confirm ? "chosen" : "selected");
            s_pending.result = "ok";
            if (s_pending.confirm)
                Do(frontEnd, s_menu.Press(Input::Confirm));
        } else {
            s_pending.result = "error: no item " + s_pending.item + " on " + ScreenName(s_pending.screen);
            Log("new front end: automation - %s", s_pending.result.c_str());
        }
    }
    if (s_state == State::Shown)
        Do(frontEnd, s_menu.Tick(dt));
}

bool OwnsMouse() { return Enabled() && !Showing() && s_mirror.Up(); }

void BeforeScreenLayout()
{
    // (the level editor: Update steps ours aside and gives the original's elements back)
    if (!Enabled() || LevelEditor::Active())
        return;
    static std::chrono::steady_clock::time_point s_last;
    static bool s_any = false;
    auto clock = std::chrono::steady_clock::now();
    float dt = s_any ? std::clamp(std::chrono::duration<float>(clock - s_last).count(), 0.0f, 0.25f) : 0.0f;
    s_last = clock;
    s_any = true;
    bool frontEnd = InFrontEnd() && !LoadingScreenUp();
    if (frontEnd && !s_frontEndWas) {
        s_awaitingFrontEnd = true;
        s_awaitTime = 0.0f;
    }
    s_frontEndWas = frontEnd;
    if (s_state == State::Launching)
        return; // (a level starting: the original's elements as the launch left them)
    if (s_state == State::Idle || s_state == State::Away) {
        UpdateMirror(dt, true);
        UpdateCover(dt);
    }
    if (s_awaitingFrontEnd) {
        s_awaitTime += dt;
        bool leftOriginal = ActiveMenu() && !s_mirrorWouldRestyle && s_holdFrames == 0;
        if (!frontEnd || s_mirror.Up() || Showing() || leftOriginal || s_awaitTime > kAwaitAtMost) {
            Log("new front end: the front end's first screen is up after %.2f s (%s)", s_awaitTime,
                s_mirror.Up() || Showing() ? "ours" : leftOriginal ? "an original screen" : "none");
            s_awaitingFrontEnd = false;
        }
    }
    // Decided here, before the screen elements are laid out for this frame's drawing: whatever the scripts made since
    // the last frame (the pause menu, a dialog, the main menu) is hidden before it is ever drawn.
    if (s_state == State::Shown || s_mirror.Up() || s_cover.On() || s_holdFrames > 0 || s_awaitingFrontEnd) {
        HideRoot(true);
        KeepRootHidden(); // (a script that scaled root_window back since - create_pause_menu's unhide_root_window)
    } else if (s_rootHidden) {
        HideRoot(false); // (nothing of ours over the original's elements any more: they're back)
    }
}

// The pause menus' level and session (the level as the data names it; the session so far).
Paint::Extras PauseExtras()
{
    Paint::Extras extras;
    const Section* section = nullptr;
    if (const Level* level = LevelPlayed(&section)) {
        extras.levelName = level->name;
        extras.levelKicker = section->tab;
        extras.levelColour = section->colour;
    }
    extras.session.seconds = s_session.seconds;
    extras.session.bestCombo = s_session.bestCombo;
    if (s_session.gapsTotal > 0) {
        extras.session.gaps = static_cast<int>(s_session.gapsLanded.size());
        extras.session.gapsTotal = s_session.gapsTotal;
    }
    return extras;
}

// The last level played, as the data names it ("": none, or not in the data) - the main menu's Continue.
std::string ContinueLevelName()
{
    int section = -1, level = -1;
    if (s_saved.lastLevel.empty() || !s_catalogueOk || !s_catalogue.Find(s_saved.lastLevel, &section, &level))
        return {};
    return s_catalogue.sections[section].levels[level].name;
}

// Continue (the restyled main menu's X / R, or its hint clicked): the last level played, in free skate.
void ContinueLastLevel(Mdl::FrontEnd* frontEnd)
{
    if (ContinueLevelName().empty())
        return;
    Sound("generic_menu_pad_choose_sound");
    Action skate{ActionKind::Skate};
    skate.levels = LevelsFor::FreeSkate;
    skate.load = s_saved.lastLevel;
    Log("new front end: continue - %s", skate.load.c_str());
    HideRoot(false);
    Do(frontEnd, skate);
}

// What a restyled screen needs besides the menu read: the pause menus' level and session, the levels' data for the
// items' art, the main menu's Continue level.
Paint::Extras RestyleExtras()
{
    Paint::Extras extras = PauseExtras();
    extras.art = ArtFor;
    extras.catalogue = s_catalogueOk ? &s_catalogue : nullptr;
    extras.continueName = ContinueLevelName();
    return extras;
}

// --- the script commands (the overlay's menu items) ---

// NewFrontend_Item item=on | restart | levels | options | editor: whether the overlay's menus make that item of ours -
// on: the new front end's main menu (its items, our Career menu); RESTART (free skate's pause menu); CHANGE LEVEL /
// the skate shop's Play Level opening our level select (free skate, with the levels' data); OPTIONS (ours); LEVEL
// EDITOR (Create-a-modes: [features] level_editor and its scripts). False while the new front end is off, and in
// network games: the menus as they were.
bool __cdecl CFunc_Item(Script::CStruct* params, Script::CScript*)
{
    if (!Enabled() || GameScreens::NetSession())
        return false;
    uint32_t item = 0;
    if (params)
        params->GetChecksum(Checksum("item"), &item, 0);
    std::string mode = GameScreens::GameModeName();
    bool freeSkate = mode == "freeskate" || mode == "singlesession";
    bool levels = s_catalogueOk && !s_catalogue.sections.empty();
    if (item == Checksum("on"))
        return true;
    if (item == Checksum("restart"))
        return !InFrontEnd() && freeSkate;
    if (item == Checksum("levels"))
        return freeSkate && levels;
    if (item == Checksum("options"))
        return true;
    if (item == Checksum("editor"))
        return InFrontEnd() && levels && LevelEditor::Enabled() && IsScript(Checksum("level_editor_enter"));
    return false;
}

// NewFrontend_Open screen=levels | editlevels | options [back=<script>]: our screen (the menu's item chose it: its own
// sound played) - shown the next frame over whatever is up; closed with Back to the restyled menu it came from, or
// `back` spawned (its menu went: the skate shop's).
bool __cdecl CFunc_Open(Script::CStruct* params, Script::CScript*)
{
    uint32_t screen = 0, back = 0;
    if (!Enabled() || !params || !params->GetChecksum(Checksum("screen"), &screen, 0))
        return false;
    params->GetChecksum(Checksum("back"), &back, 0);
    bool levels = s_catalogueOk && !s_catalogue.sections.empty();
    if (screen == Checksum("levels") && levels) {
        s_openScreen = Screen::Levels;
        s_openLevels = InFrontEnd() ? LevelsFor::FreeSkate : LevelsFor::Pause;
    } else if (screen == Checksum("editlevels") && levels) {
        s_openScreen = Screen::Levels;
        s_openLevels = LevelsFor::Edit;
    } else if (screen == Checksum("options")) {
        s_openScreen = Screen::Options;
    } else {
        return false;
    }
    s_openBack = back;
    Log("new front end: %s asked for%s", ScreenName(s_openScreen), back ? " (with a back script)" : "");
    return true;
}

void RegisterScriptFunctions()
{
    static const Script::CFunctionEntry functions[] = {
        {"NewFrontend_Item", reinterpret_cast<void*>(&CFunc_Item)},
        {"NewFrontend_Open", reinterpret_cast<void*>(&CFunc_Open)},
    };
    Script::RegisterCFunctions(functions, sizeof(functions) / sizeof(functions[0]));
}

void Draw()
{
    // (never over a tool's own UI: the level editor - Update steps ours aside)
    if (!Enabled() || LevelEditor::Active() || (!Showing() && !s_mirror.Up() && !s_cover.On() && !s_awaitingFrontEnd))
        return;
    ImGuiContext* previous = nullptr;
    if (!BeginContext(&previous))
        return;
    int w = 0, h = 0;
    UiBackend::WindowSize(&w, &h);
    if (w > 0 && h > 0) {
        Frame frame = FitFrame(static_cast<float>(w), static_cast<float>(h));
        float scale = std::round(frame.scale * 100.0f) / 100.0f;
        if (!s_fontsResolved) {
            s_fonts = ResolveFonts();
            s_fontsResolved = true;
        }
        if (scale != Paint::BuiltScale()) {
            Paint::BuildFonts(s_fonts, scale);
            UiBackend::InvalidateContextObjects(); // (the font texture made again)
            if (ImGui::GetIO().Fonts->Fonts.Size == 0) {
                // (no font at all could be read: nothing can be drawn - off for this run, the original menus back)
                Log("new front end: no font could be loaded - off for this run");
                EndContext(previous);
                HideRoot(false);
                SetState(State::Idle);
                Enabled() = false;
                return;
            }
        }
        UiBackend::NewFrame();
        ImGui::NewFrame();
        if (!Showing() && !s_mirror.Up() && !s_cover.On()) {
            // (the front end come up, its first menu not yet: its ground and the main menu's art)
            Paint::Waiting(frame, static_cast<float>(w), static_cast<float>(h), RestyleExtras(), "career");
            ImGui::Render();
            UiBackend::Render();
            EndContext(previous);
            return;
        }
        if (!Showing() && (s_mirror.Up() || s_cover.Drawn() == Cover::Over::Restyled)) {
            // (an original screen restyled - phase 3; or, under a cover, the one last up)
            Paint::Extras pause = RestyleExtras();
            if (s_mirror.Up())
                Paint::Restyled(s_mirror, frame, static_cast<float>(w), static_cast<float>(h), &s_mirrorHits, &s_mirrorScroll,
                                &pause);
            else {
                std::vector<Paint::Hit> unused;
                Paint::Restyled(s_coverTracker, frame, static_cast<float>(w), static_cast<float>(h), &unused, &s_coverScroll,
                                &pause);
            }
            ImGui::Render();
            UiBackend::Render();
            EndContext(previous);
            return;
        }
        Paint::Extras extras;
        extras.skaterName = s_skaterName;
        extras.art = ArtFor;
        extras.records = RecordsFor;
        extras.levelsMissing = !s_catalogueOk;
        extras.restartPending = s_restartPending;
        extras.optionsNote = s_optionsNote;
        static const bool showIniKeys = std::atoi(IniString("show_ini_keys", "0").c_str()) != 0;
        extras.showIniKeys = showIniKeys;
        if (s_menu.OverPause()) {
            Paint::Extras pause = PauseExtras();
            extras.levelName = pause.levelName;
            extras.levelKicker = pause.levelKicker;
            extras.levelColour = pause.levelColour;
            extras.session = pause.session;
        }
        int rows = 0;
        Paint::Screen(s_menu, frame, static_cast<float>(w), static_cast<float>(h), extras, &s_hits, &rows);
        if (rows > 0)
            s_menu.SetVisibleRows(rows);
        ImGui::Render();
        UiBackend::Render();
    }
    EndContext(previous);
}

void DeviceLost()
{
    if (!s_context)
        return;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(s_context);
    UiBackend::InvalidateContextObjects();
    ImGui::SetCurrentContext(previous);
}

std::string AutomationCommand(const std::string& line, bool* done)
{
    *done = true;
    if (!Enabled())
        return "error: the new front end is off ([features] new_frontend=0)";
    // fe select <screen>/<item> [seconds] | fe focus <screen>/<item> [seconds] | fe back | fe press <input>
    std::vector<std::string> words;
    for (size_t at = 0; at < line.size();) {
        size_t start = line.find_first_not_of(" \t", at);
        if (start == std::string::npos)
            break;
        size_t end = line.find_first_of(" \t", start);
        words.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        at = end == std::string::npos ? line.size() : end;
    }
    if (words.empty())
        return "error: fe select <screen>/<item> | fe focus <screen>/<item> | fe back | fe press <input>";
    std::string verb = words[0];
    for (char& ch : verb)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (verb == "select" || verb == "focus") {
        if (words.size() < 2 || words[1].find('/') == std::string::npos)
            return "error: fe " + verb + " <screen>/<item> [seconds]";
        std::string screenName = words[1].substr(0, words[1].find('/'));
        std::string item = words[1].substr(words[1].find('/') + 1);
        Screen screen;
        if (!ScreenFromName(screenName, &screen))
            return "error: no screen " + screenName;
        if (Refused(screen, item))
            return "refused: online (" + words[1] + ")";
        s_pending = {};
        s_pending.active = true;
        s_pending.confirm = verb == "select";
        s_pending.screen = screen;
        s_pending.item = item;
        *done = false;
        return "";
    }
    if (verb == "back") {
        if (s_state != State::Shown || s_menu.Busy())
            return "error: the new front end isn't up";
        Do(TheFrontEnd(), s_menu.Press(Input::Back));
        return "ok";
    }
    if (verb == "press") {
        static const std::pair<const char*, Input> kInputs[] = {
            {"up", Input::Up},           {"down", Input::Down},     {"left", Input::Left},
            {"right", Input::Right},     {"confirm", Input::Confirm}, {"back", Input::Back},
            {"prevtab", Input::PrevTab}, {"nexttab", Input::NextTab}};
        if (words.size() < 2)
            return "error: fe press <up|down|left|right|confirm|back|resume|prevtab|nexttab>";
        for (const auto& [name, input] : kInputs)
            if (_stricmp(words[1].c_str(), name) == 0) {
                if (s_state != State::Shown || s_menu.Busy())
                    return "error: the new front end isn't up";
                if (input == Input::Confirm && Refused(s_menu.Shown(), s_menu.SelectedId()))
                    return "refused: online";
                Do(TheFrontEnd(), s_menu.Press(input));
                return "ok";
            }
        return "error: no input " + words[1];
    }
    return "error: fe " + verb + "? (select, focus, back, press)";
}

std::string AutomationResult()
{
    if (s_pending.active)
        return {};
    return s_pending.result.empty() ? std::string("ok") : s_pending.result;
}

void AutomationCancel() { s_pending.active = false; }

bool AutomationScreenUp(const std::string& name)
{
    // (the restyled original screens: dialog, skaters, list)
    if (Enabled() && !Showing() && s_mirror.Up() && _stricmp(name.c_str(), Mirror::KindName(s_mirror.Shown().kind)) == 0)
        return true;
    Screen screen;
    return Enabled() && ScreenFromName(name, &screen) && ShownAndIdle(screen);
}

std::string AutomationStatus()
{
    if (!Enabled())
        return {};
    std::string text = std::string(StateName(s_state));
    if (s_state == State::Shown || s_state == State::Launching) {
        text += std::string(" ") + ScreenName(s_menu.Shown());
        std::string item = s_menu.SelectedId();
        if (!item.empty())
            text += "/" + item;
    }
    if (s_anchor != Anchor::None && s_state == State::Shown)
        text += std::string(" over the ") + GameScreens::AnchorName(s_anchor);
    if (!Showing() && s_mirror.Up()) {
        const Mirror::View& v = s_mirror.Shown();
        text += std::string(", restyled ") + Mirror::KindName(v.kind) + " \"" + v.title + "\"";
        if (v.selected >= 0 && v.selected < static_cast<int>(v.rows.size()))
            text += " on \"" + v.rows[v.selected].label + "\"";
    }
    return text;
}

} // namespace Front::NewFrontend
