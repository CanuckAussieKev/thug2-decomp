// Self-tests for the level editor's history / changes / search / organising (src/game/front/level_editor_actions.*,
// level_editor_changes.*, level_editor_organize.*, level_editor_clip.*; not original): the keymap's chords, key lists
// and how a press resolves; the command search's fuzzy match; the built-in registry's order; the Changes list of a
// layout and a record taken out; the Outliner's records, type filters, box tests, select-similar criteria and
// snapshot names; the clipboard's merge (sources missing, names taken); the layout keeping other modules' records.

#include <cstdint>
#include <string>
#include <vector>

#include <SDL2/SDL_scancode.h>

#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_changes.h"
#include "game/front/level_editor_clip.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_organize.h"
#include "hook/selftest.h"

namespace {

namespace Actions = Front::LevelEditor::Actions;
namespace Changes = Front::LevelEditor::Changes;
namespace Organize = Front::LevelEditor::Organize;
namespace Clip = Front::LevelEditor::Clip;
namespace Layout = Front::LevelEditor::Layout;
namespace Nodes = Front::LevelEditor::Nodes;

void TestEditorUx3Chords()
{
    Actions::Chord c;
    Check(Actions::ParseChord("Ctrl+Shift+Z", &c) && c.scancode == SDL_SCANCODE_Z &&
              c.mods == (Actions::kCtrl | Actions::kShift),
          "editor ux3: Ctrl+Shift+Z reads");
    Check(Actions::ParseChord(" control + space ", &c) && c.scancode == SDL_SCANCODE_SPACE && c.mods == Actions::kCtrl,
          "editor ux3: any case, spaces, Control");
    Check(Actions::ParseChord("Alt+[", &c) && c.scancode == SDL_SCANCODE_LEFTBRACKET && c.mods == Actions::kAlt,
          "editor ux3: punctuation keys");
    Check(Actions::ParseChord("Keypad+", &c) && c.scancode == SDL_SCANCODE_KP_PLUS && c.mods == 0,
          "editor ux3: a key whose name ends in +");
    Check(!Actions::ParseChord("Ctrl+", &c) && !Actions::ParseChord("Shift", &c) && !Actions::ParseChord("Hyper+Q", &c),
          "editor ux3: a modifier alone / an unknown word refused");
    Check(Actions::FormatChord({SDL_SCANCODE_DELETE, Actions::kCtrl | Actions::kShift}) == "Ctrl+Shift+Delete",
          "editor ux3: a chord written Ctrl, Shift, Alt then the key");
    std::vector<Actions::Chord> keys;
    Check(Actions::ParseKeys("Ctrl+Y, Ctrl+Shift+Z", &keys) && keys.size() == 2 && keys[1].scancode == SDL_SCANCODE_Z,
          "editor ux3: a key list reads");
    Check(Actions::FormatKeys(keys) == "Ctrl+Y, Ctrl+Shift+Z", "editor ux3: a key list round-trips");
    Check(Actions::ParseKeys("none", &keys) && keys.empty() && Actions::FormatKeys(keys) == "none",
          "editor ux3: none is the empty list");
    Check(Actions::ParseKeys("Ctrl+,", &keys) && keys.size() == 1 && keys[0].scancode == SDL_SCANCODE_COMMA,
          "editor ux3: a chord of the comma key isn't split");
    Check(!Actions::ParseKeys("Z, Nope", &keys), "editor ux3: a bad entry refuses the list");
}
SELFTEST(Early, TestEditorUx3Chords);

void TestEditorUx3Resolve()
{
    using Actions::Chord;
    const int Z = SDL_SCANCODE_Z, H = SDL_SCANCODE_H, G = SDL_SCANCODE_G, DEL = SDL_SCANCODE_DELETE;
    // 0 turn Z, 1 undo Ctrl+Z, 2 redo Ctrl+Shift+Z, 3 hide H, 4 show all Alt+H, 5 drop G, 6 similar Shift+G,
    // 7 delete Delete, 8 reset Ctrl+Shift+Delete
    std::vector<std::vector<Chord>> bound = {{{Z, 0}},
                                             {{Z, Actions::kCtrl}},
                                             {{SDL_SCANCODE_Y, Actions::kCtrl}, {Z, Actions::kCtrl | Actions::kShift}},
                                             {{H, 0}},
                                             {{H, Actions::kAlt}},
                                             {{G, 0}},
                                             {{G, Actions::kShift}},
                                             {{DEL, 0}},
                                             {{DEL, Actions::kCtrl | Actions::kShift}}};
    auto one = [&](int key, uint8_t held) {
        std::vector<int> r = Actions::Resolve(bound, key, held);
        return r.size() == 1 ? r[0] : -1;
    };
    Check(one(Z, 0) == 0 && one(Z, Actions::kShift) == 0, "editor ux3: Z (and Shift+Z: a qualifier) turns");
    Check(one(Z, Actions::kCtrl) == 1, "editor ux3: Ctrl+Z undoes, not turns");
    Check(one(Z, Actions::kCtrl | Actions::kShift) == 2, "editor ux3: Ctrl+Shift+Z redoes (the more specific)");
    Check(one(H, 0) == 3 && one(H, Actions::kAlt) == 4, "editor ux3: H hides, Alt+H shows all");
    Check(one(G, 0) == 5 && one(G, Actions::kShift) == 6, "editor ux3: G drops, Shift+G selects similar");
    Check(one(DEL, 0) == 7 && one(DEL, Actions::kCtrl | Actions::kShift) == 8 && one(DEL, Actions::kCtrl) == -1,
          "editor ux3: Delete deletes, Ctrl+Shift+Delete resets, Ctrl+Delete nothing");
    Check(Actions::Resolve({{{Z, 0}}, {{Z, 0}}}, Z, 0).size() == 2, "editor ux3: two bound alike both fire (a clash)");
    Check(Actions::HoldMatches({SDL_SCANCODE_W, 0}, true, Actions::kShift) &&
              !Actions::HoldMatches({SDL_SCANCODE_W, 0}, true, Actions::kCtrl) &&
              !Actions::HoldMatches({SDL_SCANCODE_W, 0}, false, 0),
          "editor ux3: a held key counts with Shift (faster), not with Ctrl, not up");
}
SELFTEST(Early, TestEditorUx3Resolve);

void TestEditorUx3Fuzzy()
{
    Check(Actions::FuzzyScore("", "Undo") == 1, "editor ux3: an empty search matches everything");
    Check(Actions::FuzzyScore("udo", "Undo") > 0 && Actions::FuzzyScore("xyz", "Undo") == 0,
          "editor ux3: letters in order match, others don't");
    Check(Actions::FuzzyScore("ndu", "Undo") == 0, "editor ux3: letters out of order don't match");
    Check(Actions::FuzzyScore("undo", "Undo") > Actions::FuzzyScore("undo", "Undo history - unrelated words") - 1 &&
              Actions::FuzzyScore("sav", "Save layout") > Actions::FuzzyScore("sav", "Show all values"),
          "editor ux3: a prefix ranks above scattered letters");
    Check(Actions::FuzzyScore("box sel", "Box select") > Actions::FuzzyScore("box sel", "Snapshots: the box elsewhere"),
          "editor ux3: words' starts rank higher (spaces ignored)");
}
SELFTEST(Early, TestEditorUx3Fuzzy);

void TestEditorUx3Changes()
{
    const char kText[] = "thug2-decomp layout 2\n"
                         "scene 4B7E41E2  # TR\n"
                         "level name=My%20TR tod=night\n"
                         "sector BF49DBEE 1 10 0 -20 0  # SkWare_Rail01\n"
                         "sector 11111111 0 0 0 0 1\n"
                         "object 22222222 1 2 3 1 0 0 0 1 0 0 0 1 scale=2\n"
                         "set 33333333 pos=1,2,3 f.0x12345678=i:4\n"
                         "node ed_crown01 GenericNode type=Crown pos=0,0,0 angles=0,0,0\n"
                         "group Mine o:22222222\n"
                         "camera ed_cam01 pos=0,0,0 angles=0,0,0 dist=10\n";
    auto label = [](uint32_t id) { return id == 0x22222222 ? std::string("Car01") : std::string(); };
    std::vector<Changes::Entry> list = Changes::List(kText, label);
    Check(list.size() == 7, "editor ux3: the changes are the level's records (not the header, scene or a group)");
    bool ok = list.size() == 7 && list[0].group == "Level properties" && list[1].keyword == "sector" &&
              list[1].id == 0xBF49DBEE && list[1].summary.find("SkWare_Rail01") != std::string::npos &&
              list[1].summary.find("90") != std::string::npos && list[2].summary.find("hidden") != std::string::npos;
    Check(ok, "editor ux3: a sector's summary: its name (the comment), moved, turned; another hidden");
    ok = list.size() == 7 && list[3].summary.find("Car01") == 0 && list[3].summary.find("scaled 2") != std::string::npos &&
         list[4].summary.find("moved") != std::string::npos && list[4].summary.find("1 field") != std::string::npos &&
         list[5].id == Changes::IdOf("ed_crown01") && list[6].group == "Cameras";
    Check(ok, "editor ux3: objects (the labeler's name), set records, added nodes by name, cameras");
    std::string without = Changes::Without(kText, list.size() == 7 ? list[1].line : 3);
    Check(without.find("BF49DBEE") == std::string::npos && without.find("11111111") != std::string::npos &&
              Changes::List(without, label).size() == 6,
          "editor ux3: a record taken out, the rest kept");
    Check(Changes::Without(kText, 99) == kText, "editor ux3: no such line: the text unchanged");
    Check(Changes::IdOf("0xDEADBEEF") == 0xDEADBEEF && Changes::IdOf("deadbeef") == 0xDEADBEEF,
          "editor ux3: hex ids as they are");
}
SELFTEST(Early, TestEditorUx3Changes);

void TestEditorUx3Records()
{
    Organize::State s;
    s.groups.push_back({"Rails by the shop", {{'s', 0xBF49DBEE}, {'o', 0x22222222}}});
    s.groups.push_back({"Empty", {}});
    Organize::SetLabel(&s, 0x11111111, "the big ramp #2");
    Organize::Switch(&s.locked, 0x22222222, true);
    Organize::Switch(&s.unpickable, 0x33333333, true);
    s.snapshot = "night-v2";
    std::string text = Organize::WriteRecords(s);
    Organize::State back;
    bool ok = true;
    for (size_t start = 0; start < text.size();) {
        size_t end = text.find('\n', start);
        ok = ok && Organize::ReadRecord(text.substr(start, end - start), &back);
        start = end + 1;
    }
    Check(ok && Organize::SameState(s, back), "editor ux3: the Outliner's records round-trip (names escaped)");
    Check(text.find('#') == std::string::npos, "editor ux3: no '#' in a record (a layout's comment mark)");
    Check(!Organize::ReadRecord("group X o:1234", &back) && !Organize::ReadRecord("snapshot autosave", &back) &&
              !Organize::ReadRecord("label 0000000G x", &back),
          "editor ux3: bad records refused");
    Organize::SetLabel(&s, 0x11111111, "");
    Check(Organize::LabelOf(s, 0x11111111).empty(), "editor ux3: an empty label clears it");
    // The layout keeps them (another module's records) and writes them back.
    Layout::File file;
    int line = 0;
    Check(Layout::Read("thug2-decomp layout 2\nscene 00000001\ngroup A o:00000002\nedlock 00000002  # x\n", &file, &line) &&
              file.extra.size() == 2 && file.extra[1] == "edlock 00000002" &&
              Layout::Write(file).find("\ngroup A o:00000002\n") != std::string::npos,
          "editor ux3: a layout keeps records it doesn't know and writes them back");
}
SELFTEST(Early, TestEditorUx3Records);

void TestEditorUx3Filters()
{
    Organize::Filter f = Organize::ParseFilter("rail: ware Spawns:");
    Check(f.families == (Nodes::Bit(Nodes::Family::Rail) | Nodes::Bit(Nodes::Family::Spawn)) && f.text == "ware",
          "editor ux3: family filters (singular / plural, any case) and the words");
    f = Organize::ParseFilter("object:car group:Shop label: locked:");
    Check(f.families == Nodes::Bit(Nodes::Family::Object) && f.text == "car" && f.group == "Shop" && f.labeled && f.locked,
          "editor ux3: a filter's value is a word; group, label, locked");
    f = Organize::ParseFilter("TRG_Rail:01");
    Check(f.families == 0 && f.text == "TRG_Rail:01", "editor ux3: a word that isn't a filter stays a word");
    f = Organize::ParseFilter("rail: ware");
    Check(Organize::RowPasses(f, Nodes::Family::Rail, "TRG_Ware_Rail01", "", false, false, false) &&
              !Organize::RowPasses(f, Nodes::Family::Spawn, "TRG_Ware_Rail01", "", false, false, false) &&
              !Organize::RowPasses(f, Nodes::Family::Rail, "TRG_Shop", "", false, false, false),
          "editor ux3: a row passes by family and words");
    f = Organize::ParseFilter("ramp");
    Check(Organize::RowPasses(f, Nodes::Family::Scenery, "Sk_01", "big Ramp", false, false, false),
          "editor ux3: words match the label too");
}
SELFTEST(Early, TestEditorUx3Filters);

void TestEditorUx3Box()
{
    Organize::Rect box = Organize::RectOf(300, 200, 100, 50);
    Check(box.x0 == 100 && box.y0 == 50 && box.x1 == 300 && box.y1 == 200, "editor ux3: a box from any two corners");
    Check(Organize::CrossingDrag(300, 100) && !Organize::CrossingDrag(100, 300),
          "editor ux3: dragged left: crossing; right: inside");
    Organize::Rect inside = {120, 60, 200, 150}, across = {250, 100, 400, 180}, away = {500, 500, 600, 600};
    Check(Organize::BoxTakes(box, inside, false) && !Organize::BoxTakes(box, across, false),
          "editor ux3: inside takes only what's wholly inside");
    Check(Organize::BoxTakes(box, across, true) && Organize::BoxTakes(box, inside, true) &&
              !Organize::BoxTakes(box, away, true),
          "editor ux3: crossing takes what touches it");
    std::vector<uint32_t> now = {1, 2, 3}, found = {3, 4};
    Check(Organize::Combine(now, found, 0) == std::vector<uint32_t>{3, 4} &&
              Organize::Combine(now, found, 1) == std::vector<uint32_t>{1, 2, 3, 4} &&
              Organize::Combine(now, found, 2) == std::vector<uint32_t>{1, 2},
          "editor ux3: a box replaces, Shift adds, Ctrl removes");
}
SELFTEST(Early, TestEditorUx3Box);

void TestEditorUx3Similar()
{
    Organize::Thing a, b;
    a.family = b.family = Nodes::Family::Object;
    a.model = "Models\\Car.mdl";
    b.model = "models\\car.MDL";
    a.cls = b.cls = 7;
    a.type = 1;
    b.type = 2;
    Check(Organize::Alike(a, b, Organize::Similar::Mesh) && Organize::Alike(a, b, Organize::Similar::Class) &&
              !Organize::Alike(a, b, Organize::Similar::Type),
          "editor ux3: objects alike by model file (any case) and class, not type");
    b.model.clear();
    Check(!Organize::Alike(a, b, Organize::Similar::Mesh), "editor ux3: no model, no mesh match");
    Organize::Thing s1, s2;
    s1.family = s2.family = Nodes::Family::Scenery;
    s1.source = 5;
    s2.source = 6;
    s1.size = {100, 20, 30};
    s2.size = {100.4f, 20, 30};
    Check(Organize::Alike(s1, s2, Organize::Similar::Mesh), "editor ux3: scenery of the same size is the same mesh");
    s2.size = {120, 20, 30};
    Check(!Organize::Alike(s1, s2, Organize::Similar::Mesh), "editor ux3: another size isn't");
    s2.source = 5;
    Check(Organize::Alike(s1, s2, Organize::Similar::Mesh), "editor ux3: a copy of the same piece is");
    std::string name;
    Check(Organize::ValidSnapshotName("night-v2") && !Organize::ValidSnapshotName("autosave") &&
              !Organize::ValidSnapshotName("a b") && !Organize::ValidSnapshotName(""),
          "editor ux3: snapshot names");
    Check(Organize::SnapshotFileName(0x4B7E41E2, "night") == "4B7E41E2.night.txt" &&
              Organize::SnapshotOf("4b7e41e2.night.TXT", 0x4B7E41E2, &name) && name == "night" &&
              !Organize::SnapshotOf("4B7E41E2.autosave.txt", 0x4B7E41E2, &name) &&
              !Organize::SnapshotOf("4B7E41E2.txt", 0x4B7E41E2, &name) &&
              !Organize::SnapshotOf("11111111.night.txt", 0x4B7E41E2, &name),
          "editor ux3: snapshot files - not the layout, not the autosave, not another level's");
    // (a snapshot isn't a saved layout: the level select's EDITED list ignores it)
    uint32_t scene = 0;
    Check(!Layout::SavedLayoutScene("4B7E41E2.night.txt", &scene), "editor ux3: a snapshot isn't an edited copy");
}
SELFTEST(Early, TestEditorUx3Similar);

void TestEditorUx3Paste()
{
    Layout::File fragment, into;
    fragment.clones.push_back({0xAAAA0001, {1, {10.0f, 0.0f, 0.0f}}});
    fragment.clones.push_back({0xAAAA0002, {0, {0.0f, 0.0f, 0.0f}}});
    Layout::CopyRecord copy = {0, 0xBBBB0001, {1, 2, 3}, {1, 0, 0, 0, 1, 0, 0, 0, 1}};
    fragment.copies.push_back(copy);
    copy.source = 0xBBBB0002;
    fragment.copies.push_back(copy);
    Front::LevelEditor::NodeCreate::Spec crown;
    crown.name = "ed_crown01";
    crown.cls = "GenericNode";
    crown.type = "Crown";
    fragment.added.push_back(crown);
    Front::LevelEditor::NodeCreate::Spec flag;
    flag.name = "TRG_CTF_Red";
    flag.cls = "GameObject";
    flag.type = "Flag_Red";
    fragment.added.push_back(flag);
    into.added.push_back(crown); // (this level has an ed_crown01 already)
    Clip::PasteHooks hooks;
    hooks.sectorExists = [](uint32_t s) { return s == 0xAAAA0001; };
    hooks.objectCopyable = [](uint32_t o) { return o == 0xBBBB0001; };
    hooks.nameTaken = [](const std::string& n) { return n == "TRG_CTF_Red"; };
    hooks.idTaken = [](uint32_t) { return false; };
    std::vector<std::string> missing;
    std::vector<uint32_t> names;
    int added = Clip::Merge(fragment, &into, hooks, &missing, &names);
    Check(added == 3 && missing.size() == 3, "editor ux3: paste - what exists here added, the rest reported");
    Check(into.clones.size() == 1 && into.clones[0].source == 0xAAAA0001 && into.clones[0].placement.quarters == 1,
          "editor ux3: paste - a scenery copy keeps its source and placement");
    Check(into.copies.size() == 1 && names.size() == 1 && into.copies[0].name == names[0] && names[0] != 0,
          "editor ux3: paste - an object copy gets a name of its own");
    Check(into.added.size() == 2 && into.added[1].name == "ed_crown02",
          "editor ux3: paste - a node whose name is taken gets a fresh one");
    Check(Clip::IsClipText(std::string(Clip::kHeaderComment) + " - 1 things\nthug2-decomp layout 2\n") &&
              !Clip::IsClipText("thug2-decomp layout 2\n"),
          "editor ux3: the clipboard's text is told from any other");
}
SELFTEST(Early, TestEditorUx3Paste);

void TestEditorUx3Registry()
{
    const std::vector<Actions::Action>& all = Actions::All();
    bool ok = static_cast<int>(all.size()) >= Actions::kBuiltinCount && all[Actions::kUndo].id == "undo" &&
              all[Actions::kPalette].id == "command_search" && all[Actions::kChainFinish].id == "chain_finish" &&
              all[Actions::kSelectSimilar].id == "select_similar" && all[Actions::kNudgeDown].kind == Actions::Kind::Hold;
    Check(ok, "editor ux3: the built-in actions in their Ids' order");
    bool unique = true;
    for (size_t i = 0; i < all.size(); ++i) {
        for (size_t j = i + 1; j < all.size(); ++j)
            unique = unique && all[i].id != all[j].id;
    }
    Check(unique, "editor ux3: every action's id is its own (the ini's keys)");
    // No two key presses of the defaults clash.
    bool clean = true;
    for (int i = 0; i < Actions::kBuiltinCount; ++i) {
        if (all[i].kind != Actions::Kind::Press)
            continue;
        for (const Actions::Chord& c : all[i].defaults) {
            for (int j = i + 1; j < Actions::kBuiltinCount; ++j) {
                if (all[j].kind != Actions::Kind::Press)
                    continue;
                for (const Actions::Chord& d : all[j].defaults)
                    clean = clean && !Actions::SameBinding(c, d);
            }
        }
    }
    Check(clean, "editor ux3: the default keys don't clash");
}
SELFTEST(Early, TestEditorUx3Registry);

} // namespace
