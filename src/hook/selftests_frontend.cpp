// Self-tests for the new front end (src/game/front/new_frontend*.cpp, not original): its data file, its state file,
// the frame and the art's crop, the layout's numbers, the menus' state machine and their motion, the fonts' metrics
// and a headless font build.

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/front/new_frontend.h"
#include "game/front/new_frontend_draw.h"
#include "game/front/new_frontend_mirror.h"
#include "game/front/new_frontend_model.h"
#include "game/front/new_frontend_options.h"
#include "game/front/new_frontend_text.h"
#include "hook/selftest.h"
#include "imgui.h"

namespace {

namespace F = Front::NewFrontend;
namespace D = Front::NewFrontend::Paint;
namespace Opt = Front::NewFrontend::Options;
namespace M = Front::NewFrontend::Mirror;

bool Near(float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) <= tolerance; }

const char* kSample = R"({
 "version": 1,
 "menu_art": {"career": "loadscrn_boston_story", "online": "loadscrn_Online"},
 "sections": [
  {"id": "thug2", "tab": "THUG2", "title": "TONY HAWK'S UNDERGROUND 2", "colour": "#A48BFF",
   "levels": [
    {"name": "TRAINING", "load": "load_TR", "art": "loadscrn_training", "level_num": "LevelNum_TR", "flag": "LEVEL_UNLOCKED_TR", "seen_flag": "LEVEL_SEEN_TR"},
    {"name": "SCHOOL", "load": "load_sc", "art": "loadscrn_school", "level_num": "LevelNum_SC", "flag": "LEVEL_UNLOCKED_SC", "hide_when_locked": true},
    {"name": "BOSTON", "load": "load_BO", "art": "loadscrn_boston", "level_num": "LevelNum_BO", "flag": "LEVEL_UNLOCKED_BO"}
   ]},
  {"id": "thps3", "tab": "THPS3", "title": "TONY HAWK'S PRO SKATER 3", "colour": "#3DB7FF",
   "levels": [
    {"name": "FOUNDRY — \"X\"", "load": "Load_thps3_foundry", "art": "loadscrn_generic_1", "level_num": 60, "flag": "LEVEL_UNLOCKED_TR", "game_tag": "THPS3", "generic_art": true}
   ]}
 ],
 "unknown": [1, 2.5, -3e2, null, false]
})";

// --- the data file ---

void TestFrontendCatalogue()
{
    F::Catalogue c;
    std::string error;
    Check(F::ParseCatalogue(kSample, &c, &error), ("front end: the sample parses: " + error).c_str());
    Check(c.sections.size() == 2 && c.sections[0].levels.size() == 3 && c.sections[1].levels.size() == 1,
          "front end: two sections, their levels");
    Check(c.sections[0].colour == 0xFFA48BFF && c.sections[1].colour == 0xFF3DB7FF, "front end: the colours");
    const F::Level& tr = c.sections[0].levels[0];
    Check(tr.name == "TRAINING" && tr.load == "load_TR" && tr.art == "loadscrn_training" && tr.levelNumName == "LevelNum_TR" &&
              tr.levelNum == -1 && tr.flag == "LEVEL_UNLOCKED_TR" && tr.seenFlag == "LEVEL_SEEN_TR" && !tr.hideWhenLocked &&
              c.sections[0].levels[1].seenFlag.empty(),
          "front end: a level's fields");
    Check(c.sections[0].levels[1].hideWhenLocked, "front end: hide_when_locked");
    const F::Level& fo = c.sections[1].levels[0];
    Check(fo.levelNum == 60 && fo.levelNumName.empty() && fo.gameTag == "THPS3" && fo.genericArt,
          "front end: a number for level_num, game_tag, generic_art");
    Check(fo.name == "FOUNDRY \xE2\x80\x94 \"X\"", "front end: \\u and \\\" escapes");
    Check(c.MenuArt("career") == "loadscrn_boston_story" && c.MenuArt("nothing").empty(), "front end: menu_art");
    int s = -1, l = -1;
    Check(c.Find("LOAD_bo", &s, &l) && s == 0 && l == 2, "front end: Find ignores case");
    Check(!c.Find("load_nothing", &s, &l), "front end: Find - none");

    F::Catalogue bad;
    Check(!F::ParseCatalogue(R"({"version": 2, "sections": []})", &bad, &error), "front end: another version refused");
    Check(!F::ParseCatalogue(R"({"version": 1})", &bad, &error), "front end: no sections refused");
    Check(!F::ParseCatalogue(R"({"version": 1, "sections": [{"id": "x", "levels": [{"name": "A"}]}]})", &bad, &error),
          "front end: a level without its load script refused");
    Check(!F::ParseCatalogue(R"({"version": 1, "sections": [)", &bad, &error) && !error.empty(),
          "front end: broken JSON refused, with why");
    Check(!F::ParseCatalogue("", &bad, &error) && !F::ParseCatalogue("[1] x", &bad, &error),
          "front end: empty text, text after the value");
    Check(F::ParseColour("#0a0B0c", 1) == 0xFF0A0B0C && F::ParseColour("#12345", 7) == 7 && F::ParseColour("123456x", 7) == 7,
          "front end: ParseColour");
}
SELFTEST(Early, TestFrontendCatalogue);

void TestFrontendState()
{
    F::SavedState s = F::ParseState("# comment\r\nlast_level = load_BO \r\nother=1\n");
    Check(s.lastLevel == "load_BO", "front end: the state file's last level");
    F::SavedState back = F::ParseState(F::FormatState(s));
    Check(back.lastLevel == "load_BO", "front end: the state file round trip");
    Check(F::ParseState("").lastLevel.empty() && F::ParseState("last_level").lastLevel.empty(),
          "front end: an empty or broken state file");
}
SELFTEST(Early, TestFrontendState);

// --- the frame, the crop, the layout ---

void TestFrontendLayout()
{
    F::Frame f = F::FitFrame(1280.0f, 720.0f);
    Check(Near(f.scale, 1.0f) && Near(f.left, 0.0f) && Near(f.top, 0.0f), "front end: 1280x720 is the design");
    f = F::FitFrame(2560.0f, 1440.0f);
    Check(Near(f.scale, 2.0f) && Near(f.X(560.0f), 1120.0f), "front end: 2560x1440 twice");
    f = F::FitFrame(1920.0f, 1200.0f);
    Check(Near(f.scale, 1.5f) && Near(f.left, 0.0f) && Near(f.top, 60.0f), "front end: 16:10 - bars above and below");
    f = F::FitFrame(3440.0f, 1440.0f);
    Check(Near(f.scale, 2.0f) && Near(f.left, 440.0f) && Near(f.top, 0.0f), "front end: 21:9 - bars at the sides");
    Check(Near(f.DesignX(f.X(123.0f)), 123.0f) && Near(f.DesignY(f.Y(45.0f)), 45.0f), "front end: window <-> design");

    // A 640 x 448 loading screen in the level select's 720 x 720 art box: its middle 70 % across, all of it down.
    F::Uv uv = F::CoverUv(640.0f, 448.0f, 720.0f, 720.0f, 0.5f, 0.0f);
    Check(Near(uv.u0, 0.15f, 0.001f) && Near(uv.u1, 0.85f, 0.001f) && Near(uv.v0, 0.0f) && Near(uv.v1, 1.0f),
          "front end: cover crop of a wide picture");
    uv = F::CoverUv(640.0f, 448.0f, 1280.0f, 300.0f, 0.5f, 0.0f);
    Check(Near(uv.u0, 0.0f) && Near(uv.u1, 1.0f) && uv.v0 > 0.3f && Near(uv.v0 + uv.v1, 1.0f, 0.001f),
          "front end: cover crop of a wide box");
    F::Uv left = F::CoverUv(640.0f, 448.0f, 720.0f, 720.0f, 0.0f, 0.03f);
    F::Uv right = F::CoverUv(640.0f, 448.0f, 720.0f, 720.0f, 1.0f, 0.03f);
    Check(Near(left.u0, 0.0f) && right.u1 <= 1.0001f && right.u0 > left.u0 && Near(left.u1 - left.u0, right.u1 - right.u0),
          "front end: the pan slides the crop across");

    // The CSS line box: the half-leading above.
    Check(Near(D::BaselineInLine(0.8f, -0.2f, 100.0f, 1.0f), 80.0f) && Near(D::BaselineInLine(0.8f, -0.2f, 100.0f, 0.92f), 76.0f),
          "front end: the baseline in a line box");
    Check(Near(D::LevelListTop(1), 121.5f, 0.05f) && D::LevelListRows(1) == 15 && D::LevelListRows(2) == 14,
          "front end: the level list's top and rows");
    Check(D::MainRowTop(6 - 1) + D::kMainRowHeight < 688.0f - 2.0f * 21.0f,
          "front end: the main menu's six rows end above its blurb");
}
SELFTEST(Early, TestFrontendLayout);

// --- the menus ---

struct Fixture {
    F::Catalogue catalogue;
    std::vector<F::SectionView> views;
    Fixture(int levels0, int levels1)
    {
        for (int s = 0; s < 2; ++s) {
            F::Section section;
            section.id = s ? "thps3" : "thug2";
            section.tab = s ? "THPS3" : "THUG2";
            section.colour = s ? 0xFF3DB7FF : 0xFFA48BFF;
            F::SectionView view;
            for (int i = 0; i < (s ? levels1 : levels0); ++i) {
                F::Level level;
                char name[32];
                std::snprintf(name, sizeof(name), "load_%c%d", s ? 'b' : 'a', i);
                level.name = name;
                level.load = name;
                section.levels.push_back(level);
                view.levels.push_back(i);
                view.locked.push_back(false);
            }
            catalogue.sections.push_back(section);
            views.push_back(view);
        }
    }
};

void Tick(F::Menu& m, float seconds, std::vector<F::Action>* actions = nullptr)
{
    for (float t = 0.0f; t < seconds; t += 0.01f) {
        F::Action a = m.Tick(0.01f);
        if (actions && a.kind != F::ActionKind::None)
            actions->push_back(a);
    }
}

void TestFrontendMenus()
{
    // Our screens are only those with no menu of the game's behind them (the level select, the settings), opened on a
    // root - Main in the front end, Pause in a level - that is never drawn: Back to it, and the caller steps ours aside.
    Fixture fx(5, 30);

    // The level select (Free Skate's): tabs, the scroll, hover, wheel, a click launches.
    F::Menu lv;
    lv.Reset(&fx.catalogue, fx.views, "load_b20", {F::Screen::Main, F::Screen::Levels});
    Check(lv.Shown() == F::Screen::Levels && lv.SectionIndex() == 1 && lv.LevelRow() == 20 &&
              lv.LevelsPurpose() == F::LevelsFor::FreeSkate,
          "front end: the level select opens at the last level played");
    lv.SetVisibleRows(13);
    lv.Press(F::Input::Down);
    Tick(lv, 0.5f);
    Check(lv.ListScroll() >= 23.0f - 13.0f - 0.01f && lv.ListScroll() <= 21.0f - 1.0f + 0.01f,
          "front end: the list scrolled to keep the selection in view");
    Check(lv.Press(F::Input::NextTab).kind == F::ActionKind::MoveSound && lv.SectionIndex() == 0 && lv.LevelRow() == 0,
          "front end: RB wraps to the first section");
    lv.Press(F::Input::Down);
    lv.Press(F::Input::PrevTab);
    Check(lv.SectionIndex() == 1 && lv.LevelRow() == 21 && Near(lv.RowAmount(21), 1.0f), "front end: LB back - its row kept");
    Check(lv.SelectId("tab:thug2") && lv.SectionIndex() == 0 && lv.SelectId("load_b3") && lv.SectionIndex() == 1 &&
              lv.LevelRow() == 3 && lv.SelectedId() == "load_b3",
          "front end: levels and tabs by id");
    Check(lv.HoverLevel(4).kind == F::ActionKind::MoveSound && lv.HoverLevel(4).kind == F::ActionKind::None,
          "front end: hovering a row selects it (once)");
    Check(lv.Wheel(1).kind == F::ActionKind::MoveSound && lv.LevelRow() == 3, "front end: the wheel");
    Check(lv.ClickLevel(2).kind == F::ActionKind::ChooseSound && lv.LaunchLoad() == "load_b2", "front end: a click launches");
    Check(lv.Press(F::Input::Back).kind == F::ActionKind::None, "front end: no input during the launch");
    std::vector<F::Action> actions;
    Tick(lv, F::kLaunchSeconds * 0.5f, &actions);
    Check(actions.empty(), "front end: Skate only once the launch is over");
    Tick(lv, F::kLaunchSeconds, &actions);
    Check(actions.size() == 1 && actions[0].kind == F::ActionKind::Skate && actions[0].load == "load_b2" &&
              actions[0].levels == F::LevelsFor::FreeSkate,
          "front end: Skate, once, with the load script");
    lv.CancelLaunch();
    // Back: a wipe to the root, which gives nothing more (ours goes aside there).
    Check(!lv.Busy() && lv.Press(F::Input::Back).kind == F::ActionKind::BackSound && lv.Wipe() >= 0.0f && lv.Busy(),
          "front end: Back from the level select wipes");
    Check(lv.Press(F::Input::Down).kind == F::ActionKind::None, "front end: no input during the wipe");
    Tick(lv, F::kWipeSeconds * 0.4f);
    Check(lv.Shown() == F::Screen::Levels, "front end: the screen changes only half-way");
    Tick(lv, F::kWipeSeconds * 0.7f);
    Check(lv.Shown() == F::Screen::Main && !lv.Busy() && lv.Press(F::Input::Back).kind == F::ActionKind::None,
          "front end: back at the root - nothing more");

    // The Level Editor's level select (Create-a-modes' LEVEL EDITOR).
    F::Menu ed;
    ed.Reset(&fx.catalogue, fx.views, "", {F::Screen::Main, F::Screen::Levels}, F::LevelsFor::Edit);
    actions.clear();
    ed.Press(F::Input::Confirm);
    Tick(ed, F::kLaunchSeconds + 0.05f, &actions);
    Check(ed.LevelsPurpose() == F::LevelsFor::Edit && actions.size() == 1 && actions[0].kind == F::ActionKind::Skate &&
              actions[0].levels == F::LevelsFor::Edit,
          "front end: a level for the editor");

    // Locked levels: listed, not skated.
    Fixture locked(3, 1);
    locked.views[0].locked[1] = true;
    F::Menu l;
    l.Reset(&locked.catalogue, locked.views, "", {F::Screen::Main, F::Screen::Levels});
    l.Press(F::Input::Down);
    Check(l.SelectedLocked() && l.Press(F::Input::Confirm).kind == F::ActionKind::None && l.Launch() < 0.0f,
          "front end: a locked level isn't started");

    // Over the pause menu: the pause's level select and the settings, drawn over the game; Back to the Pause root.
    F::Menu p;
    p.Reset(&fx.catalogue, fx.views, "", {F::Screen::Pause, F::Screen::Levels}, F::LevelsFor::Pause);
    Check(p.Shown() == F::Screen::Levels && p.LevelsPurpose() == F::LevelsFor::Pause && p.OverPause(),
          "front end: Change level - our level select, over the pause menu");
    F::Menu o;
    o.Reset(&fx.catalogue, fx.views, "", {F::Screen::Pause, F::Screen::Options});
    Check(o.Shown() == F::Screen::Options && o.OverPause(), "front end: Options over the pause menu");
    o.Press(F::Input::Back);
    Tick(o, F::kWipeSeconds + 0.05f);
    Check(o.Shown() == F::Screen::Pause, "front end: Back from Options - the Pause root");

    Check(Near(F::EaseOut(0.0f), 0.0f) && Near(F::EaseOut(1.0f), 1.0f) && Near(F::EaseOut(0.5f), 0.875f) &&
              Near(F::EaseOut(2.0f), 1.0f),
          "front end: EaseOut");
    F::Screen s;
    Check(F::ScreenFromName("Options", &s) && s == F::Screen::Options && std::string(F::ScreenName(F::Screen::Levels)) == "levels" &&
              !F::ScreenFromName("career", &s) && !F::ScreenFromName("nothing", &s),
          "front end: screen names");
}
SELFTEST(Early, TestFrontendMenus);

// --- the options screen ---

void TestFrontendOptions()
{
    // Every group's rows are options; every option is in one group; every fallback is a choice.
    std::vector<int> seen(Opt::Specs().size(), 0);
    for (const Opt::Group& g : Opt::Groups())
        for (int row : g.rows)
            ++seen[row];
    bool once = true, fallbacks = true;
    for (size_t i = 0; i < seen.size(); ++i) {
        once = once && seen[i] == 1;
        fallbacks = fallbacks && Opt::Specs()[i].fallback >= 0 &&
                    Opt::Specs()[i].fallback < static_cast<int>(Opt::Specs()[i].choices.size());
    }
    Check(once && fallbacks && Opt::Groups().size() == 5, "front end: options - five groups, each option once");
    int aniso = Opt::Find("graphics", "ANISOTROPY"), name = Opt::Find("Miscellaneous", "OnlineGameName");
    Check(aniso >= 0 && name >= 0 && Opt::Find("features", "new_frontend") >= 0 && Opt::Find("features", "thaw_bmx") >= 0 &&
              Opt::Find("features", "nothing") < 0,
          "front end: options - found by section and key (any case), new_frontend and thaw_bmx there");
    const Opt::Spec& a = Opt::Specs()[aniso];
    Check(Opt::ChoiceOf(a, "8", true) == 3 && Opt::ChoiceOf(a, " 16 ", true) == 4 && Opt::ChoiceOf(a, "5", true) == 0 &&
              Opt::ChoiceOf(a, "", false) == 0,
          "front end: options - a value's choice (a value between choices: the default)");
    const Opt::Spec& d = Opt::Specs()[Opt::Find("Graphics", "draw_distance_scale")];
    Check(Opt::ChoiceOf(d, "1.50", true) == 1 && Opt::ChoiceOf(d, "3.0", true) == 3, "front end: options - numbers as numbers");
    const Opt::Spec& n = Opt::Specs()[name];
    Check(Opt::ChoiceOf(n, "THUGPRO", true) == 1 && Opt::ChoiceOf(n, "", true) == 0, "front end: options - names in any case");

    // The model: Left / Right change the selected row's choice (clamped), Confirm cycles, LB / RB the groups.
    F::Menu m;
    m.Reset(nullptr, {}, "", {F::Screen::Main, F::Screen::Options});
    std::vector<int> values(Opt::Specs().size());
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = Opt::Specs()[i].fallback;
    m.SetOptionValues(values);
    m.SetOptionGroup(0);
    Check(m.Press(F::Input::Left).kind == F::ActionKind::None, "front end: options - nothing left of Off");
    F::Action act = m.Press(F::Input::Right);
    Check(act.kind == F::ActionKind::Option && act.option == m.OptionRows()[0] && act.value == 1 &&
              m.OptionChoice(act.option) == 1,
          "front end: options - Right: the next choice");
    m.SelectId("Graphics.msaa");
    act = m.ClickChoice(m.OptionRow(), 3);
    Check(act.kind == F::ActionKind::Option && act.value == 3 && m.ClickChoice(m.OptionRow(), 3).kind == F::ActionKind::None,
          "front end: options - a click on a choice (once)");
    act = m.Press(F::Input::Confirm);
    Check(act.kind == F::ActionKind::Option && act.value == 0, "front end: options - Confirm cycles round");
    Check(m.SelectId("game_options") && m.Press(F::Input::Confirm).kind == F::ActionKind::GameOptions,
          "front end: options - the Game options row opens the original's");
    m.Press(F::Input::NextTab);
    Check(std::string(Opt::Groups()[m.OptionGroup()].id) == "interface" && m.OptionRow() == 0, "front end: options - RB");
    Check(m.SelectId("mods.debug_mode") == false && m.SelectId("features.debug_mode") &&
              std::string(Opt::Groups()[m.OptionGroup()].id) == "mods" && m.SelectedId() == "features.debug_mode",
          "front end: options - by [section.]key, the group follows");
    m.Press(F::Input::PrevTab);
    m.Press(F::Input::PrevTab);
    m.Press(F::Input::PrevTab);
    m.Press(F::Input::PrevTab);
    m.Press(F::Input::PrevTab);
    Check(std::string(Opt::Groups()[m.OptionGroup()].id) == "mods", "front end: options - LB wraps");
}
SELFTEST(Early, TestFrontendOptions);

// The ini text changed in place: the value replaced, comments, unknown keys and other sections kept, a key or a
// section added where it's missing, the line endings kept.
void TestFrontendIni()
{
    const std::string ini = "; the settings\r\n[Graphics]\r\nAntiAliasing=1\r\nanisotropy = 4\r\n\r\n"
                            "[features]\r\n# a comment\r\nnew_frontend=1\r\nunknown_key=x\r\n\r\n[Keybinds]\r\nScreenshot=70\r\n";
    std::string v;
    Check(Opt::IniGet(ini, "graphics", "ANISOTROPY", &v) && v == "4" && !Opt::IniGet(ini, "features", "hd_ui", &v) &&
              !Opt::IniGet(ini, "Graphics", "new_frontend", &v),
          "front end: ini - read in its section only");
    std::string a = Opt::IniSet(ini, "Graphics", "anisotropy", "16");
    Check(a == "; the settings\r\n[Graphics]\r\nAntiAliasing=1\r\nanisotropy =16\r\n\r\n"
               "[features]\r\n# a comment\r\nnew_frontend=1\r\nunknown_key=x\r\n\r\n[Keybinds]\r\nScreenshot=70\r\n",
          ("front end: ini - a value replaced, all else kept: " + a).c_str());
    std::string b = Opt::IniSet(ini, "features", "hd_ui", "1");
    Check(b == "; the settings\r\n[Graphics]\r\nAntiAliasing=1\r\nanisotropy = 4\r\n\r\n"
               "[features]\r\n# a comment\r\nnew_frontend=1\r\nunknown_key=x\r\nhd_ui=1\r\n\r\n[Keybinds]\r\nScreenshot=70\r\n",
          ("front end: ini - a key added after the section's last line: " + b).c_str());
    std::string c = Opt::IniSet(ini, "online", "thugpro_peer", "1");
    Check(c == ini + "\r\n[online]\r\nthugpro_peer=1\r\n", ("front end: ini - a section added: " + c).c_str());
    std::string d = Opt::IniSet("[features]\nx=1", "features", "y", "2");
    Check(d == "[features]\nx=1\ny=2\n", ("front end: ini - \\n endings, no final break: " + d).c_str());
    std::string e = Opt::IniSet("", "Miscellaneous", "OnlineGameName", "");
    Check(e == "[Miscellaneous]\r\nOnlineGameName=\r\n" && Opt::IniGet(e, "miscellaneous", "onlinegamename", &v) && v.empty(),
          "front end: ini - an empty file, an empty value");
    std::string f = Opt::IniSet("[features]\n;new_frontend=0\nnew_frontend=0\n", "features", "new_frontend", "1");
    Check(f == "[features]\n;new_frontend=0\nnew_frontend=1\n", "front end: ini - a commented-out key left alone");
    // Round trip: every option written and read back.
    std::string text = ini;
    bool ok = true;
    for (const Opt::Spec& spec : Opt::Specs()) {
        const char* value = spec.choices.back().value;
        text = Opt::IniSet(text, spec.section, spec.key, value);
        std::string back;
        ok = ok && Opt::IniGet(text, spec.section, spec.key, &back) &&
             Opt::ChoiceOf(spec, back, true) == static_cast<int>(spec.choices.size()) - 1;
    }
    Check(ok && text.find("# a comment") != std::string::npos && text.find("unknown_key=x") != std::string::npos &&
              text.find("Screenshot=70") != std::string::npos,
          "front end: ini - every option round trips, the rest kept");
}
SELFTEST(Early, TestFrontendIni);

// --- the fonts ---

void TestFrontendFonts()
{
    // A minimal TrueType table directory: head (unitsPerEm 2048) and hhea (ascender 1900, descender -500).
    std::vector<uint8_t> ttf(12 + 2 * 16 + 2 * 64, 0);
    auto be16 = [&](size_t at, int v) {
        ttf[at] = static_cast<uint8_t>((v >> 8) & 0xFF);
        ttf[at + 1] = static_cast<uint8_t>(v & 0xFF);
    };
    auto be32 = [&](size_t at, uint32_t v) {
        be16(at, static_cast<int>(v >> 16));
        be16(at + 2, static_cast<int>(v & 0xFFFF));
    };
    be32(0, 0x00010000);
    be16(4, 2);
    size_t head = 12 + 32, hhea = head + 64;
    be32(12, 0x68656164);
    be32(12 + 8, static_cast<uint32_t>(head));
    be32(28, 0x68686561);
    be32(28 + 8, static_cast<uint32_t>(hhea));
    be16(head + 18, 2048);
    be16(hhea + 4, 1900);
    be16(hhea + 6, -500 & 0xFFFF);
    float ascent = 0.0f, descent = 0.0f;
    Check(D::ReadFontMetrics(ttf.data(), ttf.size(), &ascent, &descent) && Near(ascent, 1900.0f / 2048.0f, 0.0001f) &&
              Near(descent, -500.0f / 2048.0f, 0.0001f),
          "front end: a font's vertical metrics");
    ttf[0] = 'X';
    Check(!D::ReadFontMetrics(ttf.data(), ttf.size(), &ascent, &descent), "front end: not a font");

    // A headless build with Windows' Consolas for every face (skipped where it isn't installed).
    char windows[MAX_PATH] = {};
    UINT n = GetWindowsDirectoryA(windows, MAX_PATH);
    std::string consolas = std::string(windows, n) + "\\Fonts\\consola.ttf";
    if (GetFileAttributesA(consolas.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    D::FontFiles files;
    files.display = files.mono = files.body = consolas;
    bool built = D::BuildFonts(files, 1.0f);
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    Check(built && atlas->IsBuilt() && atlas->Fonts.Size == 9 && D::BuiltScale() == 1.0f,
          "front end: the fonts build (nine sizes)");
    ImFont* big = atlas->Fonts[0];
    Check(big && big->FindGlyphNoFallback('A') && !big->FindGlyphNoFallback('a'),
          "front end: the display face has capitals only");
    D::ForgetFonts();
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previous);
}
SELFTEST(Early, TestFrontendFonts);

void TestFrontendOff()
{
    // With the setting off (the default) nothing of it holds the game's input.
    bool& on = F::Enabled();
    bool was = on;
    on = false;
    Check(!F::BlocksGameInput() && !F::Showing(), "front end: off - the game's input untouched");
    Check(!F::OwnsMouse(), "front end: off - the mouse untouched");
    on = was;
}
SELFTEST(Early, TestFrontendOff);

// --- phase 3: the original screens restyled ---

M::ItemRead Item(uint32_t id, std::vector<std::string> texts, bool focusable = true, bool graphic = false)
{
    M::ItemRead item;
    item.id = id;
    item.texts = std::move(texts);
    item.focusable = focusable;
    item.graphicValue = graphic;
    return item;
}

void TestFrontendMirrorText()
{
    Check(M::CleanText("\\b6/\\b5=Select") == "/=Select", "restyle: button glyphs dropped");
    Check(M::CleanText("\\c2QUIT\\c0?") == "QUIT?", "restyle: colour codes dropped");
    Check(M::CleanText("Warning !\\nAny unsaved changes") == "Warning ! Any unsaved changes", "restyle: \\n a space");
    Check(M::CleanText("Warning !\\nAny", true) == "Warning !\nAny", "restyle: \\n kept as a line end");
    Check(M::CleanText("  A\\_B   C  ") == "A B C", "restyle: \\_ a space, spaces collapsed and trimmed");
    Check(M::CleanText("Songs:\t") == "Songs:", "restyle: tabs");
    Check(M::CleanText("x\\").size() == 2 && M::CleanText("").empty(), "restyle: a lone backslash at the end kept");
}
SELFTEST(Early, TestFrontendMirrorText);

void TestFrontendMirrorBuild()
{
    M::Policy policy;
    // A dialog: the title and the text block aren't rows; the buttons are; the selection follows the menu's index.
    M::MenuRead dialog;
    dialog.found = true;
    dialog.menuKey = 0x1000;
    dialog.inDialog = true;
    dialog.dialogTitle = "QUIT?";
    dialog.dialogLines = {"Are you sure?"};
    dialog.items = {Item(0, {"QUIT?"}, false), Item(0, {"Are you sure?"}, false), Item(0, {}, false),
                    Item(1, {"Yes"}), Item(2, {"No"})};
    dialog.selectedIndex = 4;
    M::View v = M::Build(dialog, policy);
    Check(v.kind == M::Kind::Dialog && v.title == "QUIT?" && v.lines.size() == 1 && v.lines[0] == "Are you sure?",
          "restyle: a dialog's title and text");
    Check(v.rows.size() == 2 && v.rows[0].label == "Yes" && v.rows[1].label == "No" && v.rows[1].index == 4 &&
              v.selected == 1,
          "restyle: a dialog's buttons and selection");
    policy.dialogs = false;
    Check(M::Build(dialog, policy).kind == M::Kind::None, "restyle: restyle_dialogs=0");
    policy = M::Policy{};
    // A dialog's buttons are restyled even with an editor up (CAS's own warnings).
    policy.editor = true;
    Check(M::Build(dialog, policy).kind == M::Kind::Dialog, "restyle: dialogs over an editor");
    policy = M::Policy{};

    // A list: label and value (a trailing ':' off), dimmed items kept, the title, other words.
    M::MenuRead list;
    list.found = true;
    list.menuId = 0x12345678;
    list.items = {Item(10, {"Songs:", "Random"}), Item(11, {"Skip Track"}), Item(12, {"Playlist"}, false)};
    list.selectedIndex = 1;
    list.title = "SOUND OPTIONS";
    list.extraTexts = {"SOUND OPTIONS", "Now playing: \\c1Song"};
    v = M::Build(list, policy);
    Check(v.kind == M::Kind::List && v.rows.size() == 3 && v.rows[0].label == "Songs" && v.rows[0].value == "Random" &&
              !v.rows[2].focusable && v.selected == 1 && v.bigName == "Skip Track",
          "restyle: a list's rows");
    Check(v.title == "SOUND OPTIONS" && v.lines.size() == 1 && v.lines[0] == "Now playing: Song",
          "restyle: a list's title and other words (the title not repeated)");
    // A gap list's row (score, name, times): the name is the label.
    {
        M::MenuRead gaps = list;
        gaps.items = {Item(20, {"25", "RAIL POP", "0"})};
        gaps.selectedIndex = 0;
        M::View g = M::Build(gaps, policy);
        Check(g.kind == M::Kind::List && g.rows[0].label == "RAIL POP" && g.rows[0].value == "25 \xC2\xB7 0",
              "restyle: the label is the first text with a letter");
    }
    // An editor's lists are restyled, lighter over its view; a horizontal menu too; a checkbox's value; decoration
    // (bars, icons) is no reason to leave a list; only screens whose meaning is a picture stay as they are.
    policy.editor = true;
    Check(M::Build(list, policy).kind == M::Kind::List && M::Build(list, policy).editor,
          "restyle: an editor's list, drawn lighter over its view");
    policy = M::Policy{};
    policy.lists = false;
    Check(M::Build(list, policy).kind == M::Kind::None, "restyle: restyle_menus=0");
    policy = M::Policy{};
    M::MenuRead other = list;
    other.vertical = false;
    Check(M::Build(other, policy).kind == M::Kind::List && M::Build(other, policy).horizontal,
          "restyle: a horizontal menu, stepped with Left / Right");
    // The pause menu: restyled as the pause (never ours over it) - every item with words, CONTINUE / QUIT in our words,
    // the rest the game's text, a not-focusable item kept dimmed.
    {
        M::MenuRead pause;
        pause.found = true;
        pause.menuId = Crc::GenerateCRCFromString("pause_vmenu");
        pause.vertical = false; // (whatever the roundbar menu says: not a list's rules)
        pause.items = {Item(Crc::GenerateCRCFromString("menu_continue"), {"CONTINUE", "CONTINUE"}),
                       Item(Crc::GenerateCRCFromString("menu_pager"), {"TEXT MESSAGES"}),
                       Item(Crc::GenerateCRCFromString("menu_save_goals"), {"SAVE GOALS"}, false),
                       Item(0, {}),
                       Item(Crc::GenerateCRCFromString("menu_skateshop"), {"QUIT"})};
        pause.items[0].graphicValue = true; // (the round bars: not a reason to leave it)
        pause.selectedIndex = 4;
        M::View p = M::Build(pause, policy);
        Check(p.kind == M::Kind::Pause && !M::OwnMenu(pause.menuId) && p.rows.size() == 4 &&
                  p.rows[0].label == std::string(F::Text::kResume) && p.rows[1].label == "TEXT MESSAGES" &&
                  !p.rows[2].focusable && p.rows[3].label == std::string(F::Text::kQuitToMenu) && p.rows[3].index == 4 &&
                  p.selected == 3 && p.bigName == std::string(F::Text::kQuitToMenu),
              "restyle: the pause menu is the game's, restyled");
    }
    other = list;
    other.items[1].texts = {"\\b6"};
    Check(M::Build(other, policy).kind == M::Kind::None, "restyle: an item without words stays");
    other = list;
    other.items[0].graphicValue = true;
    Check(M::Build(other, policy).kind == M::Kind::List, "restyle: a decorated item (a bar, an icon) is still a row");
    other = list;
    other.items[0].texts = {"Special Sounds:", ""};
    other.items[0].checkbox = 1;
    other.items[1].checkbox = 0;
    {
        M::View cb = M::Build(other, policy);
        Check(cb.kind == M::Kind::List && cb.rows[0].label == "Special Sounds" && cb.rows[0].value == F::Text::kOn &&
                  cb.rows[1].value.find(F::Text::kOff) != std::string::npos,
              "restyle: a checkbox's value");
    }
    other = list;
    other.items[2].graphicValue = true;
    other.items[2].focusable = false;
    Check(M::Build(other, policy).kind == M::Kind::List, "restyle: a picture in an item it can't focus doesn't count");
    other = list;
    other.title = "GAMMA";
    Check(M::Build(other, policy).kind == M::Kind::None, "restyle: a picture's screen (gamma) left original");
    other.title = "CREATE-A-TRICK";
    Check(M::Build(other, policy).kind == M::Kind::List, "restyle: Create-A-Trick's lists are lists");
    // A front end roundbar menu (CREATE-A-MODES: make_new_roundbar_menu's own id main_menu): Kind::Menu - our title,
    // our words, art id and note by item id, the game's text for an item we don't know; not while an editor is up.
    {
        M::MenuRead create;
        create.found = true;
        create.menuId = Crc::GenerateCRCFromString("main_menu");
        create.title = "CREATE-A-MODES";
        create.items = {Item(Crc::GenerateCRCFromString("menu_cas"), {"CREATE-A-SKATER"}, true, true),
                        Item(Crc::GenerateCRCFromString("menu_newfe_editor"), {"LEVEL EDITOR"}),
                        Item(0x1234, {"SOMETHING NEW"})};
        create.selectedIndex = 1;
        M::View c = M::Build(create, policy);
        Check(c.kind == M::Kind::Menu && c.title == std::string(F::Text::kTitleCreate) && c.rows.size() == 3 &&
                  c.rows[0].label == std::string(F::Text::kCas) && c.rows[0].art == "cas" &&
                  c.rows[1].art == "editor" && c.rows[2].label == "SOMETHING NEW" && c.rows[2].art.empty() &&
                  c.selected == 1 && c.screenArt == "create" && !c.mainMenu,
              "restyle: CREATE-A-MODES as a front end menu");
        M::MenuRead career = create;
        career.menuId = Crc::GenerateCRCFromString("newfe_career_vmenu");
        career.title = "CAREER";
        career.items = {Item(Crc::GenerateCRCFromString("menu_career"), {"STORY MODE"}),
                        Item(Crc::GenerateCRCFromString("menu_newfe_thugstory"), {"THUG STORY"}, false)};
        career.selectedIndex = 0;
        M::View k = M::Build(career, policy);
        Check(k.kind == M::Kind::Menu && k.rows.size() == 2 && k.rows[0].label == std::string(F::Text::kStoryThug2) &&
                  !k.rows[1].focusable && k.rows[1].tag == std::string(F::Text::kComingSoon),
              "restyle: our Career menu (the overlay's), its note");
        M::MenuRead main = create;
        main.menuId = Crc::GenerateCRCFromString("main_vmenu");
        main.items = {Item(Crc::GenerateCRCFromString("menu_newfe_career"), {"CAREER"}),
                      Item(Crc::GenerateCRCFromString("menu_singlesess"), {"FREE SKATE"})};
        main.selectedIndex = 1;
        M::View m = M::Build(main, policy);
        Check(m.kind == M::Kind::Menu && m.mainMenu && m.title == std::string(F::Text::kTitleMain) &&
                  m.kicker == std::string(F::Text::kMainKicker) + " \xE2\x80\x94 02" && m.bigName == std::string(F::Text::kFreeSkate),
              "restyle: the main menu's kicker numbers the item");
        M::Policy editing;
        editing.editor = true;
        Check(M::Build(create, editing).kind == M::Kind::List && M::Build(main, editing).kind == M::Kind::Menu,
              "restyle: an editor's own roundbar menu (main_menu) is one of its lists; the main menu stays ours");
    }
    other = list;
    other.items.assign(61, Item(1, {"x"}));
    Check(M::Build(other, policy).kind == M::Kind::List && M::Build(other, policy).rows.size() == 61,
          "restyle: a long list is restyled (ours scrolls it)");
    other.items.assign(401, Item(1, {"x"}));
    Check(M::Build(other, policy).kind == M::Kind::None, "restyle: past 400 items the game's own");
    // Button tags: the key's name when the glue spells it; dropped without.
    M::SetButtonNamer([](char, char digit) { return std::string(digit == '7' ? "Up" : "ESC"); });
    Check(M::CleanText("\\b7/\\b4 = Select") == "Up/ESC = Select" && M::CleanText("\\bn=Back") == "ESC=Back",
          "restyle: button tags spelled as the game does");
    M::SetButtonNamer(nullptr);
    Check(M::CleanText("\\b7/\\b4 = Select") == "/ = Select", "restyle: button tags dropped without a namer");
    // The on-screen keyboard.
    {
        M::MenuRead kb;
        kb.found = true;
        kb.keyboard = true;
        kb.keys = {{"1", "2", "3"}, {"a", "b", "c"}, {"Backspace"}};
        kb.keyRow = 1;
        kb.keyCol = 2;
        kb.entry = "SK8_";
        M::View k = M::Build(kb, policy);
        Check(k.kind == M::Kind::Keyboard && k.keys.size() == 3 && k.keys[2][0] == "Backspace" && k.keyRow == 1 &&
                  k.keyCol == 2 && k.entry == "SK8_" && k.title == std::string(F::Text::kTitleKeyboard),
              "restyle: the keyboard - its keys, the one selected, the text typed");
    }
    other = list;
    for (M::ItemRead& item : other.items)
        item.focusable = false;
    Check(M::Build(other, policy).kind == M::Kind::None, "restyle: nothing to choose stays");

    // The skater select: the profiles' display names, the stats, the name the original shows.
    M::MenuRead skaters;
    skaters.found = true;
    skaters.menuId = Crc::GenerateCRCFromString("select_skater_hmenu");
    skaters.vertical = false;
    skaters.items = {Item(1, {}), Item(2, {}), Item(3, {}, false)};
    skaters.items[0].display = "Tony Hawk";
    skaters.items[1].display = "Custom Skater";
    skaters.items[2].display = "Secret";
    skaters.selectedIndex = 1;
    skaters.skaterName = "CUSTOM SKATER";
    skaters.stats = {{"Air", 10}, {"Lip", 3}};
    v = M::Build(skaters, policy);
    Check(v.kind == M::Kind::Skaters && v.horizontal && v.rows.size() == 3 && v.rows[1].label == "Custom Skater" &&
              !v.rows[2].focusable && v.selected == 1 && v.bigName == "CUSTOM SKATER" && v.stats.size() == 2,
          "restyle: the skater select");
    skaters.skaterName.clear();
    Check(M::Build(skaters, policy).bigName == "Custom Skater", "restyle: the skater's name from the profile");
    policy.skaters = false;
    Check(M::Build(skaters, policy).kind == M::Kind::None, "restyle: restyle_skater_select=0");
    Check(M::Build(M::MenuRead{}, M::Policy{}).kind == M::Kind::None, "restyle: nothing read");
    // A menu with nothing in it yet (the scripts still making it) is unsettled - hidden a few frames; one left
    // original by the policy is not.
    {
        M::MenuRead empty;
        empty.found = true;
        empty.menuId = Crc::GenerateCRCFromString("pause_vmenu");
        M::View pending = M::Build(empty, M::Policy{});
        empty.menuId = 0x1234;
        M::View plain = M::Build(empty, M::Policy{});
        M::MenuRead words;
        words.found = true;
        words.menuId = 0x1234;
        M::ItemRead item;
        item.texts = {"Songs"};
        words.items = {item};
        M::Policy off;
        off.lists = false;
        M::View left = M::Build(words, off);
        Check(pending.kind == M::Kind::None && pending.unsettled && plain.kind == M::Kind::None && plain.unsettled &&
                  left.kind == M::Kind::None && !left.unsettled,
              "restyle: a menu not made yet is unsettled, one left original isn't");
    }
}
SELFTEST(Early, TestFrontendMirrorBuild);

void TestFrontendMirrorTracker()
{
    M::Tracker t;
    M::View v;
    v.kind = M::Kind::Dialog;
    v.key = 1;
    v.rows.resize(2);
    v.selected = 0;
    // Up after kFramesToShow frames in a row; a frame of another kind starts the count again.
    Check(!t.Update(v, 0.016f), "restyle: not up after one frame");
    Check(t.Update(v, 0.016f) && t.Up() && t.Shown().kind == M::Kind::Dialog, "restyle: up after two frames");
    // The selection's animation.
    for (int i = 0; i < 20; ++i)
        t.Update(v, 0.016f);
    Check(Near(t.RowAmount(0), 1.0f) && Near(t.RowAmount(1), 0.0f), "restyle: the selected row's amount");
    v.selected = 1;
    t.Update(v, 0.06f);
    Check(t.RowAmount(1) > 0.0f && t.RowAmount(1) < 1.0f && t.RowAmount(0) > 0.0f, "restyle: the selection moving");
    // A new menu (another key): the rows' animations start again.
    v.key = 2;
    t.Update(v, 0.0f);
    Check(Near(t.RowAmount(0), 0.0f), "restyle: a new menu, rows afresh");
    // Gone after kFramesToHide frames without it (it stays drawn meanwhile: no flicker as the original rebuilds).
    M::View none;
    Check(t.Update(none, 0.016f) && t.Update(none, 0.016f), "restyle: still up two frames after");
    Check(!t.Update(none, 0.016f) && !t.Up(), "restyle: gone after three frames");
    // Flicker: one frame of it, one without: never up.
    M::Tracker f;
    for (int i = 0; i < 6; ++i)
        f.Update(i % 2 ? none : v, 0.016f);
    Check(!f.Up(), "restyle: a screen there every other frame never comes up");
    t.Update(v, 0.0f);
    t.Update(v, 0.0f);
    t.Reset();
    Check(!t.Up() && t.Shown().kind == M::Kind::None, "restyle: Reset");
}
SELFTEST(Early, TestFrontendMirrorTracker);

// --- phase 4: the right side follows the item, the copy, the cover over hand-offs ---

void TestFrontendRightSide()
{
    // The restyled main menu: each item its own art and name - never the last level's (the user's report of 2026-10-25:
    // "CONTINUE - THPS3 / AIRPORT" whatever was highlighted); Quit too.
    std::vector<std::string> arts, names;
    for (const char* id : {"menu_newfe_career", "menu_singlesess", "menu_net", "menu_createamodes", "menu_newfe_options",
                           "menu_quit"}) {
        const M::ItemStyle* style = M::MenuItemStyle(Crc::GenerateCRCFromString(id));
        arts.push_back(style && style->ourId ? F::DefaultArt(style->ourId) : std::string());
        names.push_back(style && style->label ? style->label : "");
    }
    bool distinct = true;
    for (size_t i = 0; i < arts.size(); ++i) {
        distinct = distinct && !arts[i].empty() && !names[i].empty();
        for (size_t j = i + 1; j < arts.size(); ++j)
            distinct = distinct && arts[i] != arts[j] && names[i] != names[j];
    }
    Check(distinct, "front end: every main menu item has its own art and name (Quit too)");
    Check(M::MenuItemStyle(Crc::GenerateCRCFromString("menu_skate_now")) == nullptr && M::PauseLabel(0x1234) == std::string(),
          "front end: an item we don't know keeps the game's own words");
}
SELFTEST(Early, TestFrontendRightSide);

// No helper text, label or option line reads like a manual: no parentheses, no file names or ini keys, no " - "
// asides (the user's 2026-10-25 rule for the copy; the words are all in new_frontend_text.h).
bool PlainCopy(const std::string& s)
{
    for (const char* bad : {"(", ")", ".ini", ".json", ".ts", "[features]", "[frontend]", "_", " - ", "Pick a"})
        if (s.find(bad) != std::string::npos)
            return false;
    return true;
}

void TestFrontendCopy()
{
    bool plain = true;
    std::string offender;
    auto check = [&](const char* text) {
        if (text && !PlainCopy(text)) {
            plain = false;
            offender = text;
        }
    };
    for (const char* id : {"menu_newfe_career", "menu_singlesess", "menu_net", "menu_createamodes", "menu_newfe_options",
                           "menu_quit", "menu_career", "menu_classic", "menu_newfe_thugstory", "ss_continue_story",
                           "ss_new_story", "ss_load_story", "menu_cas", "menu_cat", "menu_creategoals", "menu_parked",
                           "menu_cagr", "menu_newfe_editor"})
        if (const M::ItemStyle* style = M::MenuItemStyle(Crc::GenerateCRCFromString(id))) {
            check(style->label);
            check(style->blurb);
            check(style->tag);
        }
    for (const char* id : {"menu_continue", "menu_retry_goal", "menu_newfe_restart", "menu_view_goals", "menu_your_options",
                           "menu_level_select", "menu_level_editor", "menu_newfe_options", "menu_skateshop"})
        check(M::PauseLabel(Crc::GenerateCRCFromString(id)));
    for (const Opt::Spec& spec : Opt::Specs()) {
        check(spec.label);
        check(spec.help);
    }
    Check(plain, ("front end: the copy is plain (no parentheses, file names, ini keys): " + offender).c_str());
    // The main menu's items need no helper line except Free Skate's; Quit has none.
    const M::ItemStyle* quit = M::MenuItemStyle(Crc::GenerateCRCFromString("menu_quit"));
    const M::ItemStyle* options = M::MenuItemStyle(Crc::GenerateCRCFromString("menu_newfe_options"));
    Check(quit && options && std::string(quit->blurb).empty() && std::string(options->blurb).empty(),
          "front end: Quit and Options say nothing more");
}
SELFTEST(Early, TestFrontendCopy);

void TestFrontendCover()
{
    using C = F::Cover;
    const float dt = 1.0f / 60.0f;
    // A hand-off to a screen that is restyled (GAME OPTIONS): covered until the restyled one is up - every frame shows
    // ours (the cover) or the restyled screen, never the original.
    C cover;
    M::Tracker t;
    M::View list;
    list.kind = M::Kind::List;
    list.key = 7;
    list.rows.resize(3);
    cover.Start(C::Over::Ours);
    bool neverBare = true;
    for (int frame = 0; frame < 30; ++frame) {
        // (frames 0-4: the original's menu not made yet; then GAME OPTIONS, which the tracker brings up 2 frames on)
        bool there = frame >= 5;
        t.Update(there ? list : M::View{}, dt);
        C::Now now;
        now.targetReady = t.Up();
        now.screen = there ? 0x1000 : 0;
        now.leftOriginal = false;
        cover.Update(now, dt);
        neverBare = neverBare && (cover.On() || t.Up());
    }
    Check(neverBare && t.Up() && !cover.On(), "front end: a hand-off to a restyled screen - covered until it is up");
    // A hand-off to a screen left original (CAS): off once it has stood still kSettleSeconds, not before.
    cover.Start(C::Over::Ours);
    C::Now cas;
    cas.leftOriginal = true;
    float shown = 0.0f;
    for (int frame = 0; frame < 120 && cover.On(); ++frame) {
        cas.screen = frame < 10 ? static_cast<uintptr_t>(0x2000 + frame) : 0x3000; // (built a menu at a time first)
        cover.Update(cas, dt);
        shown += dt;
    }
    Check(!cover.On() && shown > 10 * dt + C::kSettleSeconds - 0.02f && shown < 10 * dt + C::kSettleSeconds + 0.05f,
          "front end: a hand-off to an original screen - off once it stands still");
    // Gameplay (resume, a level loading, the level editor): off at once.
    cover.Start(C::Over::Restyled);
    C::Now play;
    play.gameplay = true;
    const char* why = nullptr;
    Check(!cover.Update(play, dt, &why) && why && cover.Drawn() == C::Over::None, "front end: the game playing ends a cover");
    // Nothing comes: off after kMaxSeconds.
    cover.Start(C::Over::Ours);
    C::Now nothing;
    int frames = 0;
    while (cover.Update(nothing, 0.1f) && frames < 100)
        ++frames;
    Check(frames >= 28 && frames <= 30, "front end: a cover lasts kMaxSeconds at most");
}
SELFTEST(Early, TestFrontendCover);

} // namespace
