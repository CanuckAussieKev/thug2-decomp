// Self-tests for the level editor's shadow rules and the selection's parts (src/game/front/level_editor_shadows.*, not
// original): the shadow names of TR, BO, THUG1's NJ and THAW's z_dt and whose they are, the place rule on TR's boxes
// (as loaded from TR.scn.xbx: g_big_rail_shadow under SkWare_Rail01, Car_Shadow beside the quarter pipe), and what a
// Ctrl+click removes.

#include <string>
#include <vector>

#include "game/front/level_editor_shadows.h"
#include "hook/selftest.h"

namespace {

namespace S = Front::LevelEditor::Shadows;
using Front::LevelEditor::V3;

bool Owners(const char* name, std::vector<std::string> expected) { return S::OwnerNames(name) == expected; }

void TestEditorShadowNames()
{
    Check(S::ShadowName("SkWare_Rail02_Shadow") && S::ShadowName("LO_forlease03shad") &&
              S::ShadowName("Z_DT_DT_AngelFlights_WireLine01_SHADOW") && !S::ShadowName("SkWare_Rail02") &&
              !S::ShadowName(nullptr),
          "editor shadows: a shadow's name has \"shad\" in it, any case");
    Check(Owners("SkWare_Rail02_Shadow", {"skware_rail02"}), "editor shadows: TR's SkWare_Rail02_Shadow");
    Check(Owners("WH_TB_Ska8brd_01b_shadow", {"wh_tb_ska8brd_01b"}), "editor shadows: _shadow (TR's skateboard)");
    Check(Owners("LO_YardGates01Shadow", {"lo_yardgates01"}), "editor shadows: Shadow with no underscore");
    Check(Owners("LO_forlease03shad", {"lo_forlease03"}), "editor shadows: shad");
    Check(Owners("Z_DT_DT_AngelFlights_WireLine01_SHADOW", {"z_dt_dt_angelflights_wireline01"}),
          "editor shadows: _SHADOW (THAW's z_dt)");
    Check(Owners("BO_Classic_Barrel_Shadow_01", {"bo_classic_barrel_01", "bo_classic_barrel01", "bo_classic_barrel"}),
          "editor shadows: BO's _Shadow_01 - the number put back first");
    Check(Owners("hj_nj_PinkhouseWiresShadow03", {"hj_nj_pinkhousewires_03", "hj_nj_pinkhousewires03",
                                                  "hj_nj_pinkhousewires"}),
          "editor shadows: NJ's Shadow03 (its owner is hj_nj_PinkhouseWires03)");
    Check(Owners("BO_Renovatr_Shado_01", {"bo_renovatr_01", "bo_renovatr01", "bo_renovatr"}),
          "editor shadows: _Shado_01");
    Check(Owners("wh_tb_shadows_554b", {"wh_tb_554b", "wh_tb554b", "wh_tb"}), "editor shadows: a number and a letter");
    Check(Owners("TB_WH_Room2_Rail02_Shadow31", {"tb_wh_room2_rail02_31", "tb_wh_room2_rail0231", "tb_wh_room2_rail02"}),
          "editor shadows: Shadow31");
    Check(Owners("Shadow02", {}) && Owners("ShadowPolyT200", {}) && Owners("SkWare_Rail02", {}) && Owners(nullptr, {}),
          "editor shadows: nothing before the suffix, or no suffix: no owner");
    Check(Owners("Rail_Shadowbox_Big", {}), "editor shadows: \"shad\" not at the end: no owner");
    Check(Owners("_Shadow", {}) && Owners("_Shadow_01", {}), "editor shadows: only an underscore before it: no owner");
    Check(Owners("Shadow_Shadow", {"shadow"}), "editor shadows: the last suffix counts");
}
SELFTEST(Early, TestEditorShadowNames);

void TestEditorShadowPlace()
{
    // TR (TR.scn.xbx's boxes).
    V3 railMin = {-646.3f, -765.4f, -3358.1f}, railMax = {-503.6f, -711.4f, -2289.0f};
    V3 shadowMin = {-638.5f, -764.7f, -3347.8f}, shadowMax = {-494.4f, -764.7f, -2286.2f};
    Check(S::FallsUnder(shadowMin, shadowMax, railMin, railMax),
          "editor shadows: g_big_rail_shadow lies under SkWare_Rail01");
    V3 qpMin = {-1620.3f, -765.4f, -4498.4f}, qpMax = {1260.2f, -587.3f, -3652.9f};
    V3 carMin = {180.3f, -762.7f, -3801.6f}, carMax = {534.3f, -762.7f, -3633.4f};
    Check(!S::FallsUnder(carMin, carMax, qpMin, qpMax),
          "editor shadows: Car_Shadow isn't the quarter pipe's (too small a part of it)");
    V3 kickerMin = {-1286.9f, -766.3f, -3410.3f}, kickerMax = {-1056.2f, -729.2f, -2945.3f};
    V3 launchMin = {-1327.0f, -764.6f, -3562.8f}, launchMax = {-1005.7f, -764.6f, -2795.7f};
    Check(S::FallsUnder(launchMin, launchMax, kickerMin, kickerMax),
          "editor shadows: wh_tb_launchramp_shado01 lies under SkWare_Kicker_Block01");
    V3 floorMin = {6514.9f, -60.9f, 1753.6f}, floorMax = {6754.9f, -56.1f, 1998.3f};
    V3 drivewayMin = {6506.1f, -58.3f, 1741.8f}, drivewayMax = {6754.2f, -58.2f, 1998.3f};
    Check(!S::FallsUnder(drivewayMin, drivewayMax, floorMin, floorMax),
          "editor shadows: a flat piece (TR's garage floor, 5 high) casts no shadow");
    // The thresholds, on a 100 x 50 x 100 box standing on y = 0.
    V3 bMin = {0, 0, 0}, bMax = {100, 50, 100};
    Check(S::FallsUnder({0, 0, 0}, {100, 0, 100}, bMin, bMax), "editor shadows: right under it");
    Check(S::FallsUnder({0, 10, 0}, {100, 10, 100}, bMin, bMax) && !S::FallsUnder({0, 10.5f, 0}, {100, 10.5f, 100}, bMin, bMax) &&
              S::FallsUnder({0, -10, 0}, {100, -10, 100}, bMin, bMax) &&
              !S::FallsUnder({0, -10.5f, 0}, {100, -10.5f, 100}, bMin, bMax),
          "editor shadows: its top within 10 of the caster's bottom");
    Check(S::FallsUnder({0, -16, 0}, {100, 0, 100}, bMin, bMax) && !S::FallsUnder({0, -16.5f, 0}, {100, 0, 100}, bMin, bMax),
          "editor shadows: no taller than 16");
    Check(!S::FallsUnder({0, 0, 0}, {100, 0, 100}, {0, 0, 0}, {100, 16, 100}) &&
              S::FallsUnder({0, 0, 0}, {100, 0, 100}, {0, 0, 0}, {100, 16.5f, 100}),
          "editor shadows: a caster taller than 16");
    // (grown by 10 + 50 = 60: a shadow from x 100 to 220 has 60 of its 120 inside - half)
    Check(S::FallsUnder({100, 0, 0}, {220, 0, 100}, bMin, bMax) && !S::FallsUnder({100, 0, 0}, {222, 0, 100}, bMin, bMax),
          "editor shadows: half of it within the footprint grown by 10 + its height");
    Check(S::FallsUnder({0, 0, 0}, {50, 0, 50}, bMin, bMax) && !S::FallsUnder({0, 0, 0}, {49, 0, 50}, bMin, bMax),
          "editor shadows: at least a quarter of the caster's footprint");
    Check(S::FallsUnder({50, 0, 0}, {50, 0, 100}, {0, 0, 0}, {100, 50, 1}),
          "editor shadows: a line under a thin caster (no width: measured along it)");
    Check(!S::FallsUnder({0, 0, 0}, {1501, 0, 100}, {0, 0, 0}, {1500, 50, 100}), "editor shadows: never wider than 1500");
    Check(!S::FallsUnder({0, 1, 0}, {100, 0, 100}, bMin, bMax), "editor shadows: not a box: no");
    // Nearest: the shadow's middle (50, 50).
    std::vector<std::pair<V3, V3>> none;
    Check(S::NearestToGroup({0, 0, 0}, {100, 0, 100}, bMin, bMax, none), "editor shadows: nobody else: the caster's");
    // (a shadow from 20 to 100: its middle (60, 60) - the caster's is (50, 50))
    std::vector<std::pair<V3, V3>> nearer = {{{50, 0, 50}, {70, 50, 70}}};
    Check(!S::NearestToGroup({20, 0, 20}, {100, 0, 100}, bMin, bMax, nearer),
          "editor shadows: inside both - the other's middle is nearer: not the caster's");
    std::vector<std::pair<V3, V3>> further = {{{0, 0, 40}, {100, 50, 200}}};
    Check(S::NearestToGroup({0, 0, 0}, {100, 0, 100}, bMin, bMax, further),
          "editor shadows: inside both - the caster's middle nearer: the caster's");
    std::vector<std::pair<V3, V3>> outside = {{{200, 0, 0}, {300, 50, 100}}};
    Check(S::NearestToGroup({0, 0, 0}, {100, 0, 100}, bMin, bMax, outside) &&
              !S::NearestToGroup({0, 0, 0}, {100, 0, 100}, {-100, 0, 0}, {-10, 50, 100},
                                 {{{40, 0, 0}, {60, 50, 100}}}),
          "editor shadows: the nearer footprint");
    Check(S::NearestToGroup({0, 0, 0}, {100, 0, 100}, {200, 0, 0}, {300, 50, 100}, {{{0, 0, 400}, {100, 50, 500}}}),
          "editor shadows: the nearer footprint, across and along (150 away, the other 350)");
    Check(S::StandsBy({0, 5, 0}, {0, -5, 0}) && !S::StandsBy({0, 5.5f, 0}, {0, -5, 0}),
          "editor shadows: standing on the shadow's floor: within 10");
    // A copy's shadow beside its caster.
    Check(S::Beside({150, 0, 0}, {200, 0, 100}, bMin, bMax) && !S::Beside({161, 0, 0}, {200, 0, 100}, bMin, bMax),
          "editor shadows: a copy's shadow within 10 + the caster's height of it");
}
SELFTEST(Early, TestEditorShadowPlace);

void TestEditorDropTarget()
{
    namespace M = Front::LevelEditor::Members;
    Check(M::DropTarget({}, {}) == -1, "editor parts: nothing under the cursor");
    Check(M::DropTarget({false}, {true}) == 0, "editor parts: a selected piece");
    Check(M::DropTarget({false}, {false}) == -1, "editor parts: an unselected piece: nothing removed");
    Check(M::DropTarget({true, false}, {true, true}) == 0,
          "editor parts: a selected shadow in front of a selected piece: the shadow");
    Check(M::DropTarget({true, false}, {false, true}) == 1,
          "editor parts: past an unselected floor / decal to the selected piece");
    Check(M::DropTarget({false, false}, {false, true}) == -1,
          "editor parts: a selected piece behind an unselected one: not removed");
    Check(M::DropTarget({true, true}, {false, true}) == 1, "editor parts: only passed pieces: the selected one");
    Check(M::DropTarget({true}, {true, true}) == 0 && M::DropTarget({false, true}, {false}) == -1 &&
              M::DropTarget({true}, {false, true}) == -1,
          "editor parts: lists of different lengths: the shorter");
}
SELFTEST(Early, TestEditorDropTarget);

} // namespace
