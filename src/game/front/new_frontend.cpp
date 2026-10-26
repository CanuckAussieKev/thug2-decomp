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
#include <map>
#include <set>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "game/core/crc.h"
#include "game/file/file.h"
#include "game/front/data_overlay.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/front/level_editor_playtest.h"
#include "game/front/level_editor_ui_backend.h"
#include "game/front/mouse_menus.h"
#include "game/front/new_frontend_draw.h"
#include "game/front/new_frontend_handoff.h"
#include "game/front/new_frontend_mirror.h"
#include "game/front/new_frontend_model.h"
#include "game/front/new_frontend_options.h"
#include "game/front/new_frontend_text.h"
#include "game/front/restart_tester.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/start_level.h"
#include "game/front/text_element.h"
#include "game/gfx/hd_ui.h"
#include "game/net/online_identity.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/profile_manager.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/config.h"
#include "game/sys/platform_system.h"
#include "game/sys/sdl_events.h"
#include "game/sys/sdl_window.h"
#include "hook/game.h"
#include "hook/log.h"
#include "imgui.h"

namespace Front::NewFrontend {

namespace {

namespace UiBackend = Front::LevelEditor::UiBackend;
using Handoff::Anchor;

uint32_t Checksum(const char* name) { return Crc::GenerateCRCFromString(name); }

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;       // load_mainmenu (the skate module's level in the front end)
constexpr uint32_t kSkater = 0x5B8AB877;             // Skater (the local skater's alias)
constexpr uint32_t kSkaterState = 0x43686585;        // SkaterState
constexpr uint32_t kSkaterScore = 0xD262802F;        // SkaterScore
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;  // SkaterCorePhysics

// --- the game's state ---

uint8_t* SkateModule() { return GameVariable<uint8_t*>(0x007CE478); }
// The level loaded: its load script's checksum (the skate module's +0x378; load_mainmenu in the front end).
uint32_t LevelLoad() { return SkateModule() ? *reinterpret_cast<uint32_t*>(SkateModule() + 0x378) : 0; }
bool InFrontEnd() { return LevelLoad() == kLoadMainMenu; }
// The skate module's career (+0x20; null before the module is up).
Obj::CSkaterCareer* Career()
{
    return SkateModule() ? *reinterpret_cast<Obj::CSkaterCareer**>(SkateModule() + 0x20) : nullptr;
}
Mdl::FrontEnd* TheFrontEnd() { return GameVariable<Mdl::FrontEnd*>(0x007CE464); }
bool Paused() { return TheFrontEnd() && TheFrontEnd()->m_paused; }
bool LoadingScreenUp()
{
    return GameVariable<uint8_t>(0x006FC49C) != 0 || GameVariable<uint32_t>(0x00786D08) != 0 ||
           GameVariable<uint8_t>(0x00786D04) != 0;
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

bool GlobalFlag(const char* name)
{
    int flag = ScriptInteger(name);
    Obj::CSkaterCareer* career = Career();
    return flag >= 0 && career && career->GetGlobalFlag(flag);
}

bool ElementThere(const char* id) { return Handoff::Find(Checksum(id)) != nullptr; }

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
    Idle,      // not up (no anchor - a level being played, the original's screens of its own)
    Shown,     // up over an anchor: drawn, with the input
    HandOff,   // a plan under way: drawn, the input held back
    Away,      // the original's screens up; back when an anchor is
    Launching, // a level started: the art up until it loads
};
const char* StateName(State state)
{
    switch (state) {
    case State::Idle: return "idle";
    case State::Shown: return "shown";
    case State::HandOff: return "handing off";
    case State::Away: return "away";
    case State::Launching: return "launching";
    }
    return "?";
}
State s_state = State::Idle;
float s_stateTime = 0.0f;
Anchor s_anchor = Anchor::None;       // the anchor the screens shown stand over
std::vector<Screen> s_returnStack;    // the screens a hand-off left from (shown again over the main menu)
LevelsFor s_returnLevels = LevelsFor::FreeSkate;
Handoff::Runner s_runner;
Handoff::Match s_lastChosen;          // a finished plan's last item: drawn until it goes
bool s_suppressPause = false;         // the original pause menu chosen: ours not over it until it goes
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
    if (Game::CGapChecklist* list = skate ? GameFunction<Game::CGapChecklist*(__thiscall*)(void*)>(0x005656C0)(skate) : nullptr) {
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

// --- the context: what the game says for the items ---

Context BuildContext()
{
    Context c;
    c.levelsListed = s_catalogueOk && !s_catalogue.sections.empty();
    c.storyStarted = GlobalFlag("CAREER_STARTED");
    c.classicStarted = GlobalFlag("CLASSIC_STARTED");
    c.levelEditor = LevelEditor::Enabled() && IsScript(Checksum("level_editor_enter"));
    std::string name = Net::GameName() ? Net::GameName() : "thps6pc";
    bool thugpro = _stricmp(name.c_str(), "thugpro") == 0;
    c.identity = thugpro ? Text::kIdentityThugpro : _stricmp(name.c_str(), "thps6pc") == 0 ? Text::kIdentityThug2 : Text::kIdentityCustom;
    c.lobby = thugpro ? Text::kLobbyThugpro : Text::kLobbyThug2;
    if (!InFrontEnd()) {
        std::string mode = Handoff::GameModeName();
        c.pauseFreeSkate = mode == "freeskate" || mode == "singlesession";
        c.pauseRetry = ElementThere("menu_retry_goal");
        c.pauseGoals = ElementThere("menu_chapters") || ElementThere("menu_view_goals");
        c.pauseYourOptions = ElementThere("menu_your_options");
        c.pauseGaps = c.pauseYourOptions; // (VIEW GAPS is in YOUR OPTIONS)
        c.pauseChangeLevel = ElementThere("menu_level_select");
        c.pauseEditor = ElementThere("menu_level_editor");
        c.pauseBmx = ElementThere("menu_thaw_bmx");
        c.pauseGameOptions = ElementThere("menu_game_options");
        c.pauseQuit = ElementThere("menu_skateshop");
        if (c.pauseBmx) {
            auto* skater = static_cast<Obj::CCompositeObject*>(Obj::FindObject(kSkater));
            Obj::CBaseComponent* core = skater ? skater->GetComponent(kSkaterCorePhysics) : nullptr;
            c.bmxLabel = core && Skater::ThawBmx::OnBike(core) ? Text::kGetOffBmx : Text::kRideBmx;
        }
    }
    return c;
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
        state == State::Shown ? " over the " : "", state == State::Shown ? Handoff::AnchorName(s_anchor) : "");
    bool wasUp = s_state == State::Shown || s_state == State::HandOff || s_state == State::Launching;
    bool up = state == State::Shown || state == State::HandOff || state == State::Launching;
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
    if (CScreenElement* root = Handoff::Find(Checksum("root_window")))
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
    CScreenElement* root = Handoff::Find(Checksum("root_window"));
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
// (the newest screen). `inDialog`: it is in dialog_box_container.
CScreenElement* ActiveMenu(bool* inDialog = nullptr)
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
        if (MouseMenus::IsMenu(e) && MouseMenus::MenuGetsPadEvents(e)) {
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

Obj::CPlayerProfileManager* ProfileManager() { return GameVariable<Obj::CPlayerProfileManager*>(0x007CE4B4); }

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
        if (CScreenElement* name = Handoff::Find(Checksum("select_skater_name")))
            if (TypeOf(name) == TextType() && reinterpret_cast<CTextElement*>(name)->m_string)
                read.skaterName = reinterpret_cast<CTextElement*>(name)->m_string;
        if (read.selectedIndex >= 0 && read.selectedIndex < static_cast<int>(read.items.size()))
            read.stats = SkaterStats(read.items[read.selectedIndex].id);
        return read;
    }
    // A list: its title (title_text, the themed menus'), the other words on its screen (under the same child of the
    // root: not the menu, its title or the helper text).
    if (CScreenElement* title = Handoff::Find(Checksum("title_text")))
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
        if (CScreenElement* e = Handoff::Find(Checksum(id)))
            skip.push_back(e);
    if (screen && screen != menu)
        CollectTexts(screen, &read.extraTexts, skip, &budget, true);
    return read;
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
    std::string mode = Handoff::GameModeName();
    policy.editor = s_leftOriginal != nullptr || mode == "parkeditor" || mode == "creategoals" ||
                    ScriptInteger("in_cat_preview_mode") == 1;
    return policy;
}

// What to restyle now (kind None: nothing).
Mirror::View MirrorNow()
{
    if (!LevelLoad() || LoadingScreenUp() || Handoff::NetSession() || LevelEditor::Active())
        return {};
    if (s_leftOriginal && std::strcmp(s_leftOriginal, "online") == 0)
        return {}; // (online: the original's screens, dialogs and all - the user tests online)
    bool inDialog = false;
    CScreenElement* menu = ActiveMenu(&inDialog);
    if (!menu)
        return {};
    static Mirror::Policy policy;
    static int policyFrames = 0;
    if (policyFrames-- <= 0) { // (the ini read twice a second)
        policy = MirrorPolicy();
        policyFrames = 30;
    }
    Mirror::Policy now = policy;
    now.editor = now.editor || s_leftOriginal != nullptr;
    Mirror::View view = Mirror::Build(ReadMenu(menu, inDialog), now);
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

void StartCover(Cover::Over over, const char* why)
{
    if (s_cover.On() && s_cover.Drawn() == over)
        return;
    Log("new front end: covering - %s (%s drawn until the next screen is ready)", why,
        over == Cover::Over::Ours ? "ours" : "the restyled screen");
    s_cover.Start(over);
    HideRoot(true);
}

// Once a frame while ours isn't drawn: the restyled screen up or down, the original's elements hidden under it.
void UpdateMirror(Mdl::FrontEnd* frontEnd, float dt, bool allowed)
{
    Mirror::View now = allowed ? MirrorNow() : Mirror::View{};
    s_mirrorWouldRestyle = now.kind != Mirror::Kind::None;
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
    if (up) {
        HideRoot(true);
        MirrorMouse(frontEnd);
    } else {
        s_mirrorEvents.clear();
    }
}

// Once a frame while a cover is on (and ours isn't up): off once the screen the hand-off went to is ready. When it ends
// with nothing restyled, the original's elements come back the same frame.
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
    if (!s_cover.Update(now, dt, &why)) {
        Log("new front end: cover off - %s (%.2f s)", why ? why : "?", s_cover.Time());
        if (!s_mirror.Up())
            HideRoot(false);
    }
}

void FreeDeviceObjects();

// Ours up over `anchor`: the screens it stands for.
void Show(Anchor anchor)
{
    LoadCatalogue();
    std::vector<Screen> stack;
    int anchorDepth = -1;
    LevelsFor levels = LevelsFor::FreeSkate;
    switch (anchor) {
    case Anchor::MainMenu:
        stack = s_returnStack.empty() ? std::vector<Screen>{Screen::Main} : s_returnStack;
        levels = s_returnLevels;
        // (a hand-off from the levels or a career sub-screen comes back one screen up: its own screen is gone)
        while (stack.size() > 1 && (stack.back() == Screen::Levels || stack.back() == Screen::Confirm ||
                                    stack.back() == Screen::Difficulty))
            stack.pop_back();
        break;
    case Anchor::CareerOptions:
        stack = {Screen::Main, Screen::Career, Handoff::GameModeName() == "classic" ? Screen::Classic : Screen::Story};
        anchorDepth = 2;
        break;
    case Anchor::CreateModes:
        stack = {Screen::Main, Screen::Create};
        anchorDepth = 1;
        break;
    case Anchor::SkateShop:
        if (!s_catalogueOk || s_catalogue.sections.empty()) {
            // (no level list: the original's level select from the skate shop menu)
            SetState(State::Away);
            return;
        }
        stack = {Screen::Main, Screen::Levels};
        anchorDepth = 1;
        break;
    case Anchor::Pause:
        stack = {Screen::Pause};
        HideRoot(true);
        break;
    case Anchor::None: return;
    }
    s_anchor = anchor;
    s_leftOriginal = nullptr;
    if (s_cover.On())
        Log("new front end: cover off - ours up");
    s_cover.Stop();
    if (s_mirror.Up())
        Log("new front end: the restyled screen gone (ours up)");
    s_mirror.Reset();
    s_mirrorHits.clear();
    s_menu.SetContext(BuildContext());
    s_menu.Reset(&s_catalogue, Views(), s_saved.lastLevel, stack, anchorDepth, levels);
    LoadOptionValues();
    s_skaterName = anchor == Anchor::Pause ? std::string() : SkaterName();
    s_hits.clear();
    s_returnStack.clear();
    s_returnLevels = LevelsFor::FreeSkate;
    SetState(State::Shown);
}

// A plan started: ours stays drawn, the input waits.
void StartPlan(Handoff::Plan plan)
{
    s_lastChosen = {};
    for (const Handoff::Step& step : plan.steps)
        if (step.kind == Handoff::Step::Kind::Choose)
            s_lastChosen = step.match;
    s_runner.Start(std::move(plan));
    SetState(State::HandOff);
}

// The screens open now (Menu keeps them; rebuilt from what is shown and its depth).
std::vector<Screen> OpenScreens()
{
    // (the menu's stack isn't exposed beyond its top and depth: the paths to each screen are fixed)
    Screen top = s_menu.Target();
    switch (top) {
    case Screen::Main: return {Screen::Main};
    case Screen::Career: return {Screen::Main, Screen::Career};
    case Screen::Story: return {Screen::Main, Screen::Career, Screen::Story};
    case Screen::Classic: return {Screen::Main, Screen::Career, Screen::Classic};
    case Screen::Difficulty:
    case Screen::Confirm:
        return {Screen::Main, Screen::Career, s_menu.Mode() ? Screen::Classic : Screen::Story};
    case Screen::Create: return {Screen::Main, Screen::Create};
    case Screen::Online: return {Screen::Main, Screen::Online};
    case Screen::Options: return {Screen::Main, Screen::Options};
    case Screen::Levels:
        if (s_menu.LevelsPurpose() == LevelsFor::Edit)
            return {Screen::Main, Screen::Create};
        return {Screen::Main};
    case Screen::Pause:
    case Screen::PauseGoals: return {Screen::Pause};
    }
    return {Screen::Main};
}

// The main menu's item for `newMenuScript` (and `gamemode`) - the first step of the front end's hand-offs - unless
// the anchor already is past it.
void FromMainMenu(Handoff::Plan* plan, const char* newMenuScript, const char* gamemode, Anchor skipWhen = Anchor::None)
{
    if (skipWhen != Anchor::None && s_anchor == skipWhen)
        return;
    std::vector<std::pair<const char*, const char*>> params = {{"new_menu_script", newMenuScript}};
    if (gamemode)
        params.emplace_back("change_gamemode", gamemode);
    plan->steps.push_back(Handoff::ChooseBy(nullptr, params));
}

void HandOff(Handoff::Plan plan)
{
    s_returnStack = OpenScreens();
    s_returnLevels = s_menu.LevelsPurpose();
    StartPlan(std::move(plan));
}

// The pause menu's own item `id` chosen (ours steps aside once it has done its work). The original's elements stay
// hidden while ours is drawn - shown again once the pause menu goes (Update), never under ours: the user's 2026-10-25
// report of both pause menus at once after GET OFF BMX. The item is chosen at once and plays its own pad_choose sound
// (add_roundbar_menu_item's pad_choose_sound - CONTINUE's the pause exit sound): ours plays none for it.
void PauseItem(const char* what, std::vector<const char*> ids, bool suppress = false)
{
    Handoff::Plan plan;
    plan.what = what;
    plan.firstAtOnce = true;
    for (const char* id : ids)
        plan.steps.push_back(Handoff::ChooseId(id));
    s_suppressPause = suppress;
    StartPlan(std::move(plan));
}

void Do(Mdl::FrontEnd* frontEnd, const Action& action)
{
    (void)frontEnd;
    switch (action.kind) {
    case ActionKind::None: return;
    case ActionKind::MoveSound: Sound("generic_menu_up_or_down_sound", "up"); return;
    case ActionKind::ChooseSound: Sound("generic_menu_pad_choose_sound"); return;
    case ActionKind::BackSound: Sound("generic_menu_pad_back_sound"); return;
    case ActionKind::FreeSkate: {
        Sound("generic_menu_pad_choose_sound");
        if (!Handoff::Exists(Handoff::ChooseBy(nullptr, {{"new_menu_script", "launch_select_skater_menu"},
                                                         {"change_gamemode", "change_gamemode_singlesession"}})
                                 .match)) {
            // (no FREE SKATE item to hand to: our level select, the skater as he is)
            Log("new front end: no FREE SKATE item in the original main menu - the level select at once");
            if (s_catalogueOk)
                s_menu.Open(Screen::Levels, LevelsFor::FreeSkate);
            return;
        }
        Handoff::Plan plan;
        plan.what = "free skate: the skater select";
        FromMainMenu(&plan, "launch_select_skater_menu", "change_gamemode_singlesession");
        HandOff(std::move(plan));
        return;
    }
    case ActionKind::CareerContinue:
    case ActionKind::CareerNew:
    case ActionKind::CareerLoad: {
        Sound("generic_menu_pad_choose_sound");
        bool classic = action.mode == 1;
        Handoff::Plan plan;
        // (over STORY / CLASSIC MODE OPTIONS of the same mode already: from there)
        bool there = s_anchor == Anchor::CareerOptions && (Handoff::GameModeName() == "classic") == classic;
        if (!there)
            FromMainMenu(&plan, "create_gamemode_options_menu", classic ? "change_gamemode_classic" : "change_gamemode_career");
        if (action.kind == ActionKind::CareerContinue) {
            plan.what = classic ? "classic: continue" : "story: continue";
            plan.steps.push_back(Handoff::ChooseId("ss_continue_story"));
        } else if (action.kind == ActionKind::CareerNew) {
            plan.what = classic ? "classic: new" : "story: new";
            plan.steps.push_back(Handoff::ChooseId("ss_new_story"));
            if (action.overwrite) // (the original's warning: Yes)
                plan.steps.push_back(Handoff::ChooseBy(nullptr, {{"new_menu_script", "reset_gamemode"}}));
            plan.steps.push_back(Handoff::ChooseBy(nullptr, {{"new_menu_script", "set_gamemode_difficulty"}},
                                                   {{"dif", action.value}}));
        } else {
            plan.what = classic ? "classic: load" : "story: load";
            plan.steps.push_back(Handoff::ChooseId("ss_load_story"));
            if (action.overwrite)
                plan.steps.push_back(Handoff::ChooseBy(nullptr, {{"new_menu_script", "reset_gamemode"}}));
        }
        if (there && !plan.steps.empty())
            plan.what += " (from the options screen)";
        HandOff(std::move(plan));
        return;
    }
    case ActionKind::Create: {
        Sound("generic_menu_pad_choose_sound");
        static const std::pair<const char*, const char*> kItems[] = {
            {"cas", "menu_cas"}, {"cat", "menu_cat"}, {"cag", "menu_creategoals"}, {"cap", "menu_parked"}, {"cagr", "menu_cagr"}};
        const char* item = nullptr;
        for (const auto& [id, element] : kItems)
            if (action.id == id)
                item = element;
        if (!item)
            return;
        Handoff::Plan plan;
        plan.what = "create: " + action.id;
        FromMainMenu(&plan, "create_createamodes_menu", nullptr, Anchor::CreateModes);
        plan.steps.push_back(Handoff::ChooseId(item));
        HandOff(std::move(plan));
        s_leftOriginal = "a create mode's editor"; // (its menus stay the original's until ours is back)
        return;
    }
    case ActionKind::Online: {
        // (only ever the player's own press: the automation channel refuses it)
        Sound("generic_menu_pad_choose_sound");
        Handoff::Plan plan;
        plan.what = "online: the original's online screens";
        FromMainMenu(&plan, "select_network_play_multi", "change_gamemode_net");
        HandOff(std::move(plan));
        s_leftOriginal = "online"; // (the online screens stay the original's)
        return;
    }
    case ActionKind::GameOptions: {
        if (s_anchor == Anchor::Pause) {
            PauseItem("pause: game options", {"menu_game_options"}); // (the item's own sound)
            return;
        }
        Sound("generic_menu_pad_choose_sound");
        Handoff::Plan plan;
        plan.what = "game options";
        FromMainMenu(&plan, "create_setup_options_menu", nullptr);
        HandOff(std::move(plan));
        return;
    }
    case ActionKind::Quit: {
        Sound("generic_menu_pad_back_sound");
        Handoff::Plan plan;
        plan.what = "quit: the original's confirm";
        FromMainMenu(&plan, "main_menu_exit_game", nullptr);
        HandOff(std::move(plan));
        return;
    }
    case ActionKind::AnchorBack: {
        Sound("generic_menu_pad_back_sound");
        Handoff::Plan plan;
        plan.what = std::string("back from the ") + Handoff::AnchorName(s_anchor);
        plan.steps.push_back(Handoff::PadBack(s_anchor));
        if (s_anchor == Anchor::SkateShop) {
            // (back from the skate shop menu: the original's skater select - the main menu after it)
            s_returnStack = {Screen::Main};
            StartPlan(std::move(plan));
            return;
        }
        plan.steps.push_back(Handoff::WaitFor(Anchor::MainMenu));
        plan.endsOurs = true;
        StartPlan(std::move(plan));
        return;
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
    case ActionKind::Pause: {
        // (no sound of ours for the original's items chosen: theirs plays - PauseItem)
        const std::string& id = action.id;
        if (id == "resume")
            PauseItem("pause: resume", {"menu_continue"});
        else if (id == "retry")
            PauseItem("pause: retry the goal", {"menu_retry_goal"});
        else if (id == "restart") {
            Handoff::Plan plan;
            plan.what = "pause: restart";
            plan.firstAtOnce = true; // (CONTINUE's sound)
            plan.steps.push_back(Handoff::ChooseId("menu_continue"));
            plan.steps.push_back(Handoff::WaitUnpaused());
            plan.steps.push_back(Handoff::RunScript("ResetSkaters"));
            StartPlan(std::move(plan));
        } else if (id == "goals")
            PauseItem("pause: goals", {ElementThere("menu_chapters") ? "menu_chapters" : "menu_view_goals"});
        else if (id == "gaps")
            PauseItem("pause: gaps", {"menu_your_options", "menu_skip_to_gap"});
        else if (id == "youroptions")
            PauseItem("pause: your options", {"menu_your_options"});
        else if (id == "changelevel")
            PauseItem("pause: change level (the original's)", {"menu_level_select"});
        else if (id == "editor")
            PauseItem("pause: the level editor", {"menu_level_editor"});
        else if (id == "bmx")
            PauseItem("pause: the BMX", {"menu_thaw_bmx"});
        else if (id == "quit")
            PauseItem("pause: quit (the original's confirm)", {"menu_skateshop"});
        else if (id == "original") {
            Sound("generic_menu_pad_choose_sound");
            Log("new front end: the original pause menu (ours aside until it goes)");
            HideRoot(false);
            s_suppressPause = true;
            SetState(State::Away);
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
        case SDL_SCANCODE_X: case SDL_SCANCODE_R:
            if (!repeat)
                Do(frontEnd, s_menu.Press(Input::Resume));
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
        case SDL_CONTROLLER_BUTTON_X: Do(frontEnd, s_menu.Press(Input::Resume)); break;
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
            case Paint::Hit::Continue: Do(frontEnd, s_menu.ClickContinue()); break;
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
            (event->type == SDL_MOUSEMOTION || event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEWHEEL))
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
    if (CScreenElement* item = Handoff::Find(Checksum("menu_level_editor"))) {
        Log("new front end: the pause menu's LEVEL EDITOR chosen");
        Handoff::LaunchPad(frontEnd, Mdl::FrontEnd::kPadChoose, item);
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

// The things the channel never does through ours: anything online (the Online screen's hand-off).
bool Refused(Screen screen, const std::string& item)
{
    return screen == Screen::Online && _stricmp(item.c_str(), "play") == 0;
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

bool Showing() { return s_state == State::Shown || s_state == State::HandOff || s_state == State::Launching; }

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

    Anchor anchor = Handoff::Current(frontEnd);
    if (anchor != Anchor::Pause)
        s_suppressPause = s_suppressPause && Handoff::Find(Checksum("pause_vmenu")) != nullptr && Paused();
    if (s_rootHidden && !(anchor == Anchor::Pause && (s_state == State::Shown || s_state == State::HandOff)) &&
        !s_mirror.Up() && !s_cover.On())
        HideRoot(false); // (ours not over the pause menu any more, nothing restyled or covered: the original's elements back)
    // The menu the pad reaches now: a dialog box over an anchor, or another menu over the pause menu (a sub-screen that
    // keeps pause_vmenu - VIEW GOALS, the level select...), and ours isn't over it (the dialog / screen is restyled, or
    // left as it is).
    bool inDialog = false;
    CScreenElement* active = ActiveMenu(&inDialog);
    bool pauseActive = !active || IdOfElement(active) == Checksum("pause_vmenu");
    bool usable = anchor != Anchor::None && !LoadingScreenUp() && !inDialog &&
                  !(anchor == Anchor::Pause && (s_suppressPause || s_edit != Edit::Off || !pauseActive));

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
        UpdateMirror(frontEnd, dt, true);
        UpdateCover(dt);
        KeepRootHidden();
        return;
    case State::HandOff: {
        s_menu.Tick(dt);
        bool failed = false;
        if (s_runner.Running()) {
            if (!s_runner.Update(frontEnd, dt, &failed)) {
                KeepRootHidden();
                return;
            }
            if (failed) {
                // (the original didn't go: ours again over whatever is up, or aside)
                Anchor up = Handoff::Current(frontEnd);
                if (up != Anchor::None) {
                    Show(up);
                } else {
                    SetState(State::Away);
                    StartCover(Cover::Over::Ours, "a hand-off gave up");
                }
                return;
            }
            if (s_runner.Current().endsOurs) {
                Show(Handoff::Current(frontEnd));
                return;
            }
            s_stateTime = 0.0f;
        }
        // Done. An original screen to restyle came up (a dialog - QUIT?, a confirmation -, a list): ours aside at once.
        UpdateMirror(frontEnd, dt, true);
        if (s_mirror.Up()) {
            SetState(State::Away);
            return;
        }
        // The pause menu's own item chosen, and the pause menu still the screen the pad reaches (the item did its
        // work and left the game paused - the BMX's toggle before 2026-10-25 -, or came back to it): ours over it
        // again at once, never both.
        if (s_anchor == Anchor::Pause && anchor == Anchor::Pause && usable && active && s_stateTime > 0.5f) {
            Log("new front end: the original's pause menu still up after the hand-off - ours over it again");
            Show(Anchor::Pause);
            KeepRootHidden();
            return;
        }
        // Else still drawn until the last item chosen goes (the original's screen changing under ours), 3 s at most.
        bool lastThere = (s_lastChosen.id || s_lastChosen.script || !s_lastChosen.checksums.empty()) &&
                         Handoff::Exists(s_lastChosen);
        if (!lastThere || s_stateTime > 3.0f) {
            SetState(State::Away);
            // (ours stays on screen, the original's elements hidden, until the screen it went to is ready - no
            // original screen in between: the user's report of 2026-10-25, GAME OPTIONS flashed THUG2's menu)
            StartCover(Cover::Over::Ours, "a hand-off is done");
            if (!LevelLoad() || !InFrontEnd())
                FreeDeviceObjects();
        }
        return;
    }
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
    if (anchor != s_anchor || !usable) {
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
    // What the game says (once a second is plenty: flags, the pause menu's items).
    static float contextTime = 0.0f;
    contextTime += dt;
    if (contextTime > 0.5f) {
        contextTime = 0.0f;
        s_menu.SetContext(BuildContext());
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

void Draw()
{
    // (never over a tool's own UI: the level editor - Update steps ours aside)
    if (!Enabled() || LevelEditor::Active() || (!Showing() && !s_mirror.Up() && !s_cover.On()))
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
        if (!Showing() && (s_mirror.Up() || s_cover.Drawn() == Cover::Over::Restyled)) {
            // (an original screen restyled - phase 3; or, under a cover, the one last up)
            if (s_mirror.Up())
                Paint::Restyled(s_mirror, frame, static_cast<float>(w), static_cast<float>(h), &s_mirrorHits, &s_mirrorScroll);
            else {
                std::vector<Paint::Hit> unused;
                Paint::Restyled(s_coverTracker, frame, static_cast<float>(w), static_cast<float>(h), &unused, &s_coverScroll);
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
            {"resume", Input::Resume},   {"prevtab", Input::PrevTab}, {"nexttab", Input::NextTab}};
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
    if (s_state == State::Shown || s_state == State::HandOff || s_state == State::Launching) {
        text += std::string(" ") + ScreenName(s_menu.Shown());
        std::string item = s_menu.SelectedId();
        if (!item.empty())
            text += "/" + item;
    }
    if (s_anchor != Anchor::None && s_state == State::Shown)
        text += std::string(" over the ") + Handoff::AnchorName(s_anchor);
    if (!Showing() && s_mirror.Up()) {
        const Mirror::View& v = s_mirror.Shown();
        text += std::string(", restyled ") + Mirror::KindName(v.kind) + " \"" + v.title + "\"";
        if (v.selected >= 0 && v.selected < static_cast<int>(v.rows.size()))
            text += " on \"" + v.rows[v.selected].label + "\"";
    }
    return text;
}

} // namespace Front::NewFrontend
