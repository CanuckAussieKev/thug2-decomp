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
    Check(D::MainRowTop(F::kMainItems - 1) + D::kMainRowHeight < 688.0f - 2.0f * 21.0f,
          "front end: the main menu's rows end above its blurb");
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
    Fixture fx(5, 30);
    F::Menu m;
    F::Context ctx;
    ctx.levelsListed = true;
    m.SetContext(ctx);
    m.Reset(&fx.catalogue, fx.views, "load_b20");
    Check(m.Shown() == F::Screen::Main && m.MainSelected() == F::kMainFreeSkate && m.CanContinue() &&
              m.SelectedId() == "freeskate",
          "front end: starts at the main menu on Free Skate, Continue there");
    Check(Near(m.RowAmount(F::kMainFreeSkate), 1.0f), "front end: the first selection drawn at once");
    Check(m.Press(F::Input::Confirm).kind == F::ActionKind::FreeSkate, "front end: Free Skate hands over (the skater select)");
    Check(m.Press(F::Input::Down).kind == F::ActionKind::MoveSound && m.SelectedId() == "online",
          "front end: Down moves, with its sound");
    m.Press(F::Input::Down);
    m.Press(F::Input::Down);
    Check(m.SelectedId() == "options", "front end: Options after Create");
    m.Press(F::Input::Down);
    Check(m.SelectedId() == "quit" && m.Press(F::Input::Down).kind == F::ActionKind::None,
          "front end: Quit last; the list doesn't go past its end");
    Check(m.Press(F::Input::Confirm).kind == F::ActionKind::Quit, "front end: Quit is QUIT's confirm");
    Check(m.Press(F::Input::Back).kind == F::ActionKind::Quit, "front end: Back on the main menu is QUIT's confirm");

    // The row animation: the old row eases out, the new one in, in kSelectSeconds.
    m.Press(F::Input::Up); // (Options - not ticked, so its row is still out)
    m.Press(F::Input::Up); // (Create)
    Tick(m, 0.06f);
    float mid = m.RowAmount(F::kMainCreate);
    Check(mid > 0.6f && mid < 0.95f && m.RowAmount(F::kMainOptions) < 0.4f, "front end: the selection half-way (eased)");
    Tick(m, 0.08f);
    Check(Near(m.RowAmount(F::kMainCreate), 1.0f) && Near(m.RowAmount(F::kMainOptions), 0.0f),
          "front end: the selection snapped in 120 ms");

    // Career: a wipe to its screen; THUG Story skipped (in development).
    m.HoverItem(F::kMainCareer);
    Check(m.Press(F::Input::Confirm).kind == F::ActionKind::ChooseSound && m.Wipe() >= 0.0f && m.Busy(),
          "front end: Career wipes");
    Check(m.Press(F::Input::Down).kind == F::ActionKind::None, "front end: no input during the wipe");
    Tick(m, F::kWipeSeconds * 0.4f);
    Check(m.Shown() == F::Screen::Main, "front end: the screen changes only half-way");
    Tick(m, F::kWipeSeconds * 0.7f);
    Check(m.Shown() == F::Screen::Career && !m.Busy() && m.Items().size() == 3 && !m.Items()[2].enabled &&
              m.Items()[2].tag == Front::NewFrontend::Text::kComingSoon,
          "front end: the career screen - THUG Story listed, coming soon");
    m.Press(F::Input::Down);
    Check(m.SelectedId() == "classic" && m.Press(F::Input::Down).kind == F::ActionKind::None &&
              m.ClickItem(2).kind == F::ActionKind::None && !m.SelectId("thugstory"),
          "front end: THUG Story isn't selectable");

    // Story, not started: Continue disabled, New -> the difficulty -> CareerNew with the game's numbers.
    m.Press(F::Input::Up);
    m.Press(F::Input::Confirm);
    Tick(m, F::kWipeSeconds + 0.05f);
    Check(m.Shown() == F::Screen::Story && !m.Items()[0].enabled && m.SelectedId() == "new",
          "front end: story - nothing to continue, New selected");
    m.Press(F::Input::Confirm);
    Tick(m, F::kWipeSeconds + 0.05f);
    Check(m.Shown() == F::Screen::Difficulty && m.Items().size() == 3 && m.SelectedId() == "normal",
          "front end: the story's difficulty - Easy, Normal, Sick; Normal first");
    m.Press(F::Input::Down);
    F::Action a = m.Press(F::Input::Confirm);
    Check(a.kind == F::ActionKind::CareerNew && a.mode == 0 && a.value == 2 && !a.overwrite,
          "front end: a new story on Sick (2), nothing to overwrite");
    Check(m.Press(F::Input::Back).kind == F::ActionKind::BackSound, "front end: Back from the difficulty");
    Tick(m, F::kWipeSeconds + 0.05f);
    Check(m.Shown() == F::Screen::Story, "front end: the story screen again");
    m.Press(F::Input::Down);
    a = m.Press(F::Input::Confirm);
    Check(a.kind == F::ActionKind::CareerLoad && a.mode == 0 && !a.overwrite, "front end: load, nothing started");

    // Started: Continue; New and Load ask first; Classic's difficulties are Normal 0 / Sick 1.
    ctx.storyStarted = ctx.classicStarted = true;
    m.SetContext(ctx);
    Check(m.Items()[0].enabled && m.SelectId("continue") && m.Press(F::Input::Confirm).kind == F::ActionKind::CareerContinue,
          "front end: started - Continue");
    m.SelectId("new");
    m.Press(F::Input::Confirm);
    Tick(m, F::kWipeSeconds + 0.05f);
    Check(m.Shown() == F::Screen::Confirm && m.SelectedId() == "no", "front end: a new story over a started one asks (No first)");
    m.Press(F::Input::Confirm); // (No)
    Tick(m, F::kWipeSeconds + 0.05f);
    Check(m.Shown() == F::Screen::Story, "front end: No - back");
    m.SelectId("load");
    m.Press(F::Input::Confirm);
    Tick(m, F::kWipeSeconds + 0.05f);
    m.SelectId("yes");
    a = m.Press(F::Input::Confirm);
    Check(m.ConfirmLoad() && a.kind == F::ActionKind::CareerLoad && a.overwrite, "front end: load over a started story: yes");
    m.Press(F::Input::Back);
    Tick(m, F::kWipeSeconds + 0.05f);
    m.Press(F::Input::Back);
    Tick(m, F::kWipeSeconds + 0.05f);
    Check(m.Shown() == F::Screen::Career, "front end: back to the career screen");
    m.SelectId("classic");
    m.Press(F::Input::Confirm);
    Tick(m, F::kWipeSeconds + 0.05f);
    m.SelectId("new");
    m.Press(F::Input::Confirm);
    Tick(m, F::kWipeSeconds + 0.05f);
    m.SelectId("yes");
    m.Press(F::Input::Confirm);
    Tick(m, F::kWipeSeconds + 0.05f);
    Check(m.Shown() == F::Screen::Difficulty && m.Items().size() == 2 && m.SelectedId() == "normal",
          "front end: classic's difficulty - Normal, Sick");
    a = m.Press(F::Input::Confirm);
    Check(a.kind == F::ActionKind::CareerNew && a.mode == 1 && a.value == 0 && a.overwrite,
          "front end: a new classic career on Normal (0), the started one reset");

    // Create: the hand-offs; the Level Editor opens the level select for editing (when it's on).
    F::Menu cr;
    ctx.levelEditor = true;
    cr.SetContext(ctx);
    cr.Reset(&fx.catalogue, fx.views, "", {F::Screen::Main, F::Screen::Create}, 1);
    Check(cr.Shown() == F::Screen::Create && cr.Items().size() == 6, "front end: Create's six items");
    a = cr.Press(F::Input::Confirm);
    Check(a.kind == F::ActionKind::Create && a.id == "cas", "front end: Create-A-Skater hands over");
    cr.SelectId("editor");
    cr.Press(F::Input::Confirm);
    Tick(cr, F::kWipeSeconds + 0.05f);
    Check(cr.Shown() == F::Screen::Levels && cr.LevelsPurpose() == F::LevelsFor::Edit, "front end: the editor's level select");
    std::vector<F::Action> actions;
    cr.Press(F::Input::Confirm);
    Tick(cr, F::kLaunchSeconds + 0.05f, &actions);
    Check(actions.size() == 1 && actions[0].kind == F::ActionKind::Skate && actions[0].levels == F::LevelsFor::Edit,
          "front end: a level for the editor");
    cr.CancelLaunch();
    cr.Press(F::Input::Back);
    Tick(cr, F::kWipeSeconds + 0.05f);
    Check(cr.Shown() == F::Screen::Create && cr.Press(F::Input::Back).kind == F::ActionKind::AnchorBack,
          "front end: Back from Create over CREATE-A-MODES is the original's back");
    Tick(cr, F::kWipeSeconds + 0.05f);
    Check(cr.Shown() == F::Screen::Main && cr.Press(F::Input::Back).kind == F::ActionKind::Quit,
          "front end: ... and then the main menu's (no anchor any more)");
    ctx.levelEditor = false;
    cr.SetContext(ctx);
    cr.Reset(&fx.catalogue, fx.views, "", {F::Screen::Main, F::Screen::Create});
    Check(!cr.SelectId("editor"), "front end: the Level Editor off");

    // Online: Play online hands over; Identity opens the options' Online group.
    F::Menu on;
    on.Reset(&fx.catalogue, fx.views, "", {F::Screen::Main, F::Screen::Online});
    Check(on.Press(F::Input::Confirm).kind == F::ActionKind::Online, "front end: Play online");
    on.SelectId("identity");
    on.Press(F::Input::Confirm);
    Tick(on, F::kWipeSeconds + 0.05f);
    Check(on.Shown() == F::Screen::Options && std::string(Opt::Groups()[on.OptionGroup()].id) == "online",
          "front end: Identity -> Options, Online");

    // The level select: tabs, the scroll, hover, wheel, a click launches.
    F::Menu lv;
    lv.SetContext(ctx);
    lv.Reset(&fx.catalogue, fx.views, "load_b20", {F::Screen::Main, F::Screen::Levels}, 1);
    Check(lv.Shown() == F::Screen::Levels && lv.SectionIndex() == 1 && lv.LevelRow() == 20,
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
    actions.clear();
    Tick(lv, F::kLaunchSeconds * 0.5f, &actions);
    Check(actions.empty(), "front end: Skate only once the launch is over");
    Tick(lv, F::kLaunchSeconds, &actions);
    Check(actions.size() == 1 && actions[0].kind == F::ActionKind::Skate && actions[0].load == "load_b2" &&
              actions[0].levels == F::LevelsFor::FreeSkate,
          "front end: Skate, once, with the load script");
    lv.CancelLaunch();
    Check(!lv.Busy() && lv.Press(F::Input::Back).kind == F::ActionKind::AnchorBack,
          "front end: Back over the skate shop menu is the original's back");

    // Locked levels: listed, not skated.
    Fixture locked(3, 1);
    locked.views[0].locked[1] = true;
    F::Menu l;
    l.Reset(&locked.catalogue, locked.views, "", {F::Screen::Main, F::Screen::Levels});
    Check(!l.CanContinue(), "front end: no Continue without a level");
    l.Press(F::Input::Down);
    Check(l.SelectedLocked() && l.Press(F::Input::Confirm).kind == F::ActionKind::None && l.Launch() < 0.0f,
          "front end: a locked level isn't started");

    // Continue from the main menu: the launch, then Skate with the last level.
    F::Menu c;
    c.Reset(&fx.catalogue, fx.views, "LOAD_A3");
    Check(c.ClickContinue().kind == F::ActionKind::ChooseSound, "front end: Continue launches");
    actions.clear();
    Tick(c, F::kLaunchSeconds + 0.05f, &actions);
    Check(actions.size() == 1 && actions[0].kind == F::ActionKind::Skate && actions[0].load == "LOAD_A3",
          "front end: Continue starts the last level");

    // The pause menu: its rows from what the original's has; Back resumes; Goals and gaps; Change level ours in free
    // skate; Options over it.
    F::Context pc;
    pc.levelsListed = true;
    pc.pauseFreeSkate = true;
    pc.pauseChangeLevel = pc.pauseGaps = pc.pauseYourOptions = pc.pauseQuit = pc.pauseBmx = true;
    pc.bmxLabel = "Get off BMX";
    F::Menu p;
    p.SetContext(pc);
    p.Reset(&fx.catalogue, fx.views, "", {F::Screen::Pause});
    std::vector<std::string> ids;
    for (const F::Item& it : p.Items())
        ids.push_back(it.id);
    Check(ids == std::vector<std::string>{"resume", "restart", "goals", "changelevel", "bmx", "options", "original", "quit"} &&
              p.Items()[4].label == "Get off BMX",
          "front end: the pause menu's rows");
    a = p.Press(F::Input::Back);
    Check(a.kind == F::ActionKind::Pause && a.id == "resume", "front end: Back on the pause menu resumes");
    p.SelectId("restart");
    a = p.Press(F::Input::Confirm);
    Check(a.kind == F::ActionKind::Pause && a.id == "restart", "front end: Restart");
    p.SelectId("goals");
    p.Press(F::Input::Confirm);
    Tick(p, F::kWipeSeconds + 0.05f);
    Check(p.Shown() == F::Screen::PauseGoals && p.Items().size() == 2 && p.SelectedId() == "gaps" && p.OverPause(),
          "front end: Goals and gaps (no goals in free skate: gaps, your options)");
    p.Press(F::Input::Back);
    Tick(p, F::kWipeSeconds + 0.05f);
    p.SelectId("changelevel");
    p.Press(F::Input::Confirm);
    Tick(p, F::kWipeSeconds + 0.05f);
    Check(p.Shown() == F::Screen::Levels && p.LevelsPurpose() == F::LevelsFor::Pause && p.OverPause(),
          "front end: Change level - our level select, over the pause menu");
    p.Press(F::Input::Back);
    Tick(p, F::kWipeSeconds + 0.05f);
    pc.pauseFreeSkate = false; // (a career: the original's level select)
    p.SetContext(pc);
    p.SelectId("changelevel");
    a = p.Press(F::Input::Confirm);
    Check(a.kind == F::ActionKind::Pause && a.id == "changelevel" && !p.SelectId("restart"),
          "front end: in a career, Change level is the original's and there's no Restart");
    p.SelectId("options");
    p.Press(F::Input::Confirm);
    Tick(p, F::kWipeSeconds + 0.05f);
    Check(p.Shown() == F::Screen::Options && p.OverPause(), "front end: Options over the pause menu");

    // No data: the level select can't open from Create.
    F::Menu empty;
    empty.Reset(nullptr, {}, "", {F::Screen::Main, F::Screen::Create});
    empty.SelectId("cagr");
    Check(empty.Press(F::Input::Confirm).id == "cagr", "front end: Create without data");
    Check(Near(F::EaseOut(0.0f), 0.0f) && Near(F::EaseOut(1.0f), 1.0f) && Near(F::EaseOut(0.5f), 0.875f) &&
              Near(F::EaseOut(2.0f), 1.0f),
          "front end: EaseOut");
    F::Screen s;
    Check(F::ScreenFromName("PauseGoals", &s) && s == F::Screen::PauseGoals && std::string(F::ScreenName(F::Screen::Online)) == "online" &&
              !F::ScreenFromName("nothing", &s),
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
    Check(built && atlas->IsBuilt() && atlas->Fonts.Size == 7 && D::BuiltScale() == 1.0f,
          "front end: the fonts build (seven sizes)");
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
    // Lists left original: an editor, a horizontal menu, ours, an item without words, a slider, a title, too many.
    policy.editor = true;
    Check(M::Build(list, policy).kind == M::Kind::None, "restyle: no lists while an editor is up");
    policy = M::Policy{};
    policy.lists = false;
    Check(M::Build(list, policy).kind == M::Kind::None, "restyle: restyle_menus=0");
    policy = M::Policy{};
    M::MenuRead other = list;
    other.vertical = false;
    Check(M::Build(other, policy).kind == M::Kind::None, "restyle: a horizontal menu stays");
    other = list;
    other.menuId = Crc::GenerateCRCFromString("pause_vmenu");
    Check(M::Build(other, policy).kind == M::Kind::None && M::OwnMenu(other.menuId), "restyle: the pause menu is ours");
    other = list;
    other.items[1].texts = {"\\b6"};
    Check(M::Build(other, policy).kind == M::Kind::None, "restyle: an item without words stays");
    other = list;
    other.items[0].graphicValue = true;
    Check(M::Build(other, policy).kind == M::Kind::None && !M::Build(other, policy).why.empty(),
          "restyle: a slider / checkbox stays (with a reason)");
    other = list;
    other.items[2].graphicValue = true;
    other.items[2].focusable = false;
    Check(M::Build(other, policy).kind == M::Kind::List, "restyle: a picture in an item it can't focus doesn't count");
    other = list;
    other.title = "CREATE-A-TRICK";
    Check(M::Build(other, policy).kind == M::Kind::None, "restyle: a title left original");
    other = list;
    other.items.push_back(Item(Crc::GenerateCRCFromString("menu_cas"), {"Create-A-Skater"}));
    Check(M::Build(other, policy).kind == M::Kind::None, "restyle: CREATE-A-MODES is ours");
    other = list;
    other.items.assign(61, Item(1, {"x"}));
    Check(M::Build(other, policy).kind == M::Kind::None, "restyle: too many items stay");
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
    // The main menu with a level to continue: each item its own art, kicker and name - never the last level's
    // (the user's report of 2026-10-25: "CONTINUE - THPS3 / AIRPORT" whatever was highlighted).
    Fixture fx(5, 30);
    F::Menu m;
    F::Context ctx;
    ctx.levelsListed = true;
    m.SetContext(ctx);
    m.Reset(&fx.catalogue, fx.views, "load_b20");
    Check(m.CanContinue() && F::ContinueName(m) == "load_b20", "front end: Continue's level for the hints row");
    std::vector<std::string> arts, names;
    bool follows = true;
    for (int i = 0; i < F::kMainItems; ++i) {
        m.HoverItem(i);
        F::Feature f = F::FeatureFor(m);
        follows = follows && f.name == m.Items()[i].label && !f.art.empty() && !f.launching &&
                  f.kicker.find("Continue") == std::string::npos && f.name != "load_b20";
        arts.push_back(f.art);
        names.push_back(f.name);
    }
    bool distinct = true;
    for (size_t i = 0; i < arts.size(); ++i)
        for (size_t j = i + 1; j < arts.size(); ++j)
            distinct = distinct && arts[i] != arts[j] && names[i] != names[j];
    Check(follows, "front end: the main menu's right side is the highlighted item's");
    Check(distinct, "front end: every main menu item has its own art and name (Quit too)");
    m.HoverItem(F::kMainQuit);
    Check(F::FeatureFor(m).kicker.find(Front::NewFrontend::Text::kMainKicker) == 0 &&
              F::FeatureFor(m).kicker.find("06") != std::string::npos,
          "front end: the main menu's kicker numbers the item");
    // Continue launched: the last level's art and name, then (the launch over) the item again.
    m.ClickContinue();
    F::Feature launch = F::FeatureFor(m);
    Check(launch.launching && launch.name == "load_b20" && launch.kicker == "THPS3", "front end: Continue's launch shows its level");
    // Another screen: the career's items each theirs too.
    F::Menu career;
    career.SetContext(ctx);
    career.Reset(&fx.catalogue, fx.views, "load_b20", {F::Screen::Main, F::Screen::Career});
    career.HoverItem(1);
    F::Feature c = F::FeatureFor(career);
    Check(c.name == career.Items()[1].label && c.art == F::DefaultArt("classic") && F::ContinueName(career) == "load_b20",
          "front end: the career screen's right side follows its item");
    // The pause screens show the level played (not an item): nothing from FeatureFor.
    F::Menu p;
    p.Reset(&fx.catalogue, fx.views, "", {F::Screen::Pause});
    Check(F::FeatureFor(p).name.empty() && F::FeatureFor(p).art.empty(), "front end: the pause screen keeps the level");
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
    F::Context c;
    c.levelsListed = c.levelEditor = c.storyStarted = true;
    c.pauseRetry = c.pauseGoals = c.pauseGaps = c.pauseYourOptions = c.pauseChangeLevel = c.pauseFreeSkate = true;
    c.pauseEditor = c.pauseBmx = c.pauseGameOptions = c.pauseQuit = true;
    c.identity = "THUG2";
    c.lobby = "Beenox";
    bool plain = true;
    std::string offender;
    for (int s = 0; s < F::kScreens; ++s)
        for (int mode = 0; mode < 2; ++mode)
            for (bool load : {false, true})
                for (const F::Item& it : F::ItemsFor(static_cast<F::Screen>(s), c, mode, load))
                    for (const std::string& text : {it.label, it.blurb, it.tag, it.detail})
                        if (!PlainCopy(text)) {
                            plain = false;
                            offender = text;
                        }
    c.levelEditor = false;
    for (const F::Item& it : F::ItemsFor(F::Screen::Create, c, 0, false))
        if (!PlainCopy(it.blurb)) {
            plain = false;
            offender = it.blurb;
        }
    for (const Opt::Spec& spec : Opt::Specs())
        for (const char* text : {spec.label, spec.help})
            if (!PlainCopy(text)) {
                plain = false;
                offender = text;
            }
    Check(plain, ("front end: the copy is plain (no parentheses, file names, ini keys): " + offender).c_str());
    // The main menu's items need no helper line except Free Skate's; Quit has none.
    std::vector<F::Item> main = F::ItemsFor(F::Screen::Main, c, 0, false);
    Check(main[F::kMainQuit].blurb.empty() && main[F::kMainOptions].blurb.empty(), "front end: Quit and Options say nothing more");
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
