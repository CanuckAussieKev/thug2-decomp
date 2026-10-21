// Self-tests for the level editor's edited copies and Reset level (src/game/front/level_editor_layout.*, not original):
// which level gets its layout (only the one picked as its edited copy, never in a network session), what a pick and a
// level coming up leave of the request, the network guard's signs, the level select's text for an edited copy, which
// files of the layouts' folder are edited copies and the scene a level structure names - and the layouts' rail records
// for a rail point moved by itself.

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor_layout.h"
#include "hook/selftest.h"

namespace {

namespace Layout = Front::LevelEditor::Layout;

constexpr uint32_t kLoadTR = 0x11111111, kLoadBO = 0x22222222; // (two load scripts' checksums)

// --- which level gets its layout ------------------------------------------------------------------------------------

void TestEditorLevelsApply()
{
    Check(Layout::ApplyEditedCopy(kLoadTR, kLoadTR, false), "editor levels: the edited copy picked gets its layout");
    Check(!Layout::ApplyEditedCopy(0, kLoadTR, false), "editor levels: the level as shipped (nothing picked) gets none");
    Check(!Layout::ApplyEditedCopy(kLoadTR, kLoadBO, false),
          "editor levels: another level than the one picked gets none");
    Check(!Layout::ApplyEditedCopy(kLoadTR, kLoadTR, true), "editor levels: never in a network session");
    Check(!Layout::ApplyEditedCopy(0, 0, false), "editor levels: no level, no request: none");

    // A pick: the edited copy only when asked for and offline.
    Check(Layout::RequestFromPick(kLoadTR, true, false) == kLoadTR, "editor levels: picking the edited copy keeps it");
    Check(Layout::RequestFromPick(kLoadTR, false, false) == 0, "editor levels: picking the level as shipped clears it");
    Check(Layout::RequestFromPick(kLoadTR, true, true) == 0,
          "editor levels: an edited copy picked in a network session is refused");

    // A level coming up: the request kept for that level only.
    Check(Layout::RequestAfterLoad(kLoadTR, kLoadTR, false) == kLoadTR,
          "editor levels: the edited copy loaded again stays the edited copy");
    Check(Layout::RequestAfterLoad(kLoadTR, kLoadBO, false) == 0, "editor levels: another level ends the request");
    Check(Layout::RequestAfterLoad(kLoadTR, kLoadTR, true) == 0, "editor levels: a network session ends the request");

    // A run of them: TR's edited copy, then Boston, then TR from anywhere else - shipped.
    uint32_t request = Layout::RequestFromPick(kLoadTR, true, false);
    bool first = Layout::ApplyEditedCopy(request, kLoadTR, false);
    request = Layout::RequestAfterLoad(request, kLoadTR, false);
    bool again = Layout::ApplyEditedCopy(request, kLoadTR, false); // (TR loaded again: retry)
    request = Layout::RequestAfterLoad(request, kLoadTR, false);
    bool boston = Layout::ApplyEditedCopy(request, kLoadBO, false);
    request = Layout::RequestAfterLoad(request, kLoadBO, false);
    bool back = Layout::ApplyEditedCopy(request, kLoadTR, false); // (TR by career / a script: no pick)
    Check(first && again && !boston && !back && request == 0,
          "editor levels: edited TR, TR again, Boston, then TR without a pick - only the first two edited");
    // A network game hosting the level picked as edited: shipped, and the request gone with it.
    request = Layout::RequestFromPick(kLoadTR, true, false);
    bool net = Layout::ApplyEditedCopy(request, kLoadTR, true);
    request = Layout::RequestAfterLoad(request, kLoadTR, true);
    Check(!net && request == 0 && !Layout::ApplyEditedCopy(request, kLoadTR, false),
          "editor levels: a network game over the edited copy's level: shipped, and not edited after it either");

    // The network guard's signs: any one is a session.
    Check(!Layout::NetworkSession(false, false, false) && Layout::NetworkSession(true, false, false) &&
              Layout::NetworkSession(false, true, false) && Layout::NetworkSession(false, false, true),
          "editor levels: online, a network game or a network scene each block layouts");
}
SELFTEST(Early, TestEditorLevelsApply);

// --- the level select's entries and the layouts' folder ----------------------------------------------------------------

void TestEditorLevelsFiles()
{
    Check(Layout::EditedText("TRAINING") == "TRAINING (EDITED)" && Layout::EditedText("") == "LEVEL (EDITED)" &&
              Layout::EditedText(nullptr) == "LEVEL (EDITED)",
          "editor levels: the level select's text for an edited copy");
    // (map4) The entry's title and its sign's label: the edited copy's name, else "<LEVEL> (EDITED)" / "EDITED".
    Check(Layout::EditedTitle("TRAINING", "") == "TRAINING (EDITED)" && Layout::EditedTitle("TRAINING", "My TR") == "My TR" &&
              Layout::EditedTitle(nullptr, "") == "LEVEL (EDITED)" && Layout::SignLabel("") == "EDITED" &&
              Layout::SignLabel("My TR") == "My TR" && Layout::SignLabel("12345678901234") == "12345678901234" &&
              Layout::SignLabel("123456789012345") == "1234567890123.",
          "editor levels: the edited entry's title and sign label (its name)");
    uint32_t scene = 0;
    Check(Layout::SavedLayoutScene("4B7E41E2.txt", &scene) && scene == 0x4B7E41E2,
          "editor levels: TR's saved layout is an edited copy (scene 4B7E41E2)");
    Check(Layout::SavedLayoutScene("4b7e41e2.TXT", &scene) && scene == 0x4B7E41E2,
          "editor levels: hex digits and the extension in either case");
    Check(!Layout::SavedLayoutScene("4B7E41E2.autosave.txt", &scene) &&
              !Layout::SavedLayoutScene("4B7E41E2.txt.tmp", &scene) && !Layout::SavedLayoutScene("4B7E41G2.txt", &scene) &&
              !Layout::SavedLayoutScene("4B7E41E.txt", &scene) && !Layout::SavedLayoutScene("4B7E41E20.txt", &scene) &&
              !Layout::SavedLayoutScene("notes.txt", &scene),
          "editor levels: autosaves, temporary files and other names aren't edited copies");
    Check(Layout::SavedFileName(0x4B7E41E2) == "4B7E41E2.txt" &&
              Layout::SavedLayoutScene(Layout::SavedFileName(0x0000ABCD), &scene) && scene == 0x0000ABCD,
          "editor levels: the saved file's name read back");
    // (levels.qb's Level_TR: level="TR" - the scene CFunc_LoadScene names by it, the user's TR layout's)
    Check(Layout::SceneOfLevelText("TR") == 0x4B7E41E2 && Layout::SceneOfLevelText("tr") == 0x4B7E41E2 &&
              Layout::SceneOfLevelText("") == 0 && Layout::SceneOfLevelText(nullptr) == 0,
          "editor levels: a level structure's level= text gives its scene");
}
SELFTEST(Early, TestEditorLevelsFiles);

// --- rail records: a rail point moved by itself ---------------------------------------------------------------------

void TestEditorLevelsRailRecords()
{
    Layout::File file;
    file.scene = 0x4B7E41E2;
    // (SkWare_Rail01 moved: its sector and its three rail points by the same offset; one point then moved by itself)
    file.sectors.push_back({0xBF49DBEE, {0, {-188.212769f, 0.0f, -13.4348145f}}, false});
    file.rails.push_back({0x835E979B, {0, {-188.212769f, 0.0f, -13.4348145f}}, false});
    file.rails.push_back({0x1A57C621, {0, {-150.5f, 24.25f, -13.4348145f}}, false});
    file.rails.push_back({0xFE2963DE, {0, {-188.212769f, 0.0f, -13.4348145f}}, true});
    std::string text = Layout::Write(file);
    Check(text.find("\nrail 1A57C621 0 -150.5 24.25 -13.4348145 0") != std::string::npos,
          "editor levels: a rail point moved by itself is a rail record (a plain move)");
    Layout::File back;
    int line = -1;
    bool same = Layout::Read(text, &back, &line) && back.rails.size() == 3 && back.sectors.size() == 1;
    for (size_t i = 0; same && i < 3; ++i) {
        const auto& a = file.rails[i].placement;
        const auto& b = back.rails[i].placement;
        same = back.rails[i].name == file.rails[i].name && a.quarters == b.quarters && a.offset.x == b.offset.x &&
               a.offset.y == b.offset.y && a.offset.z == b.offset.z && back.rails[i].hidden == file.rails[i].hidden;
    }
    Check(same, "editor levels: rail records read back exactly (offsets, hidden)");
    // The user's TR layout as it is on disk still reads (it is TR's edited copy now).
    const char kUsers[] = "thug2-decomp layout 2\nscene 4B7E41E2\n"
                          "sector BF49DBEE 0 -188.212769 0 -13.4348145 0  # SkWare_Rail01\n"
                          "rail 1A57C621 0 -188.212769 0 -13.4348145 0  # TRG_Ware_Rail02\n"
                          "rail 835E979B 0 -188.212769 0 -13.4348145 0  # TRG_Ware_Rail01\n"
                          "rail FE2963DE 0 -188.212769 0 -13.4348145 0  # TRG_Ware_Rail51\n";
    Check(Layout::Read(kUsers, &back, &line) && back.scene == 0x4B7E41E2 && back.sectors.size() == 1 &&
              back.rails.size() == 3,
          "editor levels: the user's TR layout reads as before");
}
SELFTEST(Early, TestEditorLevelsRailRecords);

} // namespace
