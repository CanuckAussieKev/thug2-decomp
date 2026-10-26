#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "game/front/level_editor.h"

// NOT ORIGINAL - the level editor's shadows and the selection's parts (ours; see level_editor.h).
//
// Shadows: the levels bake a piece's shadow into a separate see-through decal sector lying on the floor (TR has 36:
// SkWare_Rail02_Shadow, WH_TB_Foldingchair_01_Shadow, g_big_rail_shadow...), so moving the piece left its shadow
// behind. A scenery group (level_editor_scenery.h's SelectGroup / AddGroup, and the hover's outline) now takes its
// shadows too, which then move, turn, scale, go Home and undo as the group's other members do:
//   (a) by name, always first: a sector whose name ends in a shadow suffix - "shad" with any letters after it
//       (_Shadow, Shadow, _shadow, _SHADOW, _Shado, shad, _Shadows), then optionally "_" and a number and a letter -
//       belongs to the sector named by what's before it (OwnerNames: with the number put back first - BO's
//       BO_Classic_Barrel_Shadow_01 is BO_Classic_Barrel_01's -, then without). Its owner is the first of those that is
//       a sector of the scene; when that is in the group, so is the shadow.
//   (b) by place, only for shadow-named pieces (the name has "shad" in it) with no owner by name in the scene: a
//       see-through, flat piece lying on the floor under one of the group's pieces (FallsUnder: on the floor it stands
//       on, mostly within its footprint grown by its height, and covering a fair part of it) and no nearer to another
//       piece standing there than to that one (NearestToGroup). Conservative: an unnamed decal, graffiti or a puddle
//       never joins, nor a small shadow beside a big piece (TR's Car_Shadow by the warehouse's quarter pipe).
// The thresholds below come from TR's and BO's pieces.
namespace Front::LevelEditor::Shadows {

// A shadow's flatness: its box no taller than this.
constexpr float kFlat = 16.0f;
// On the floor under the caster: the shadow's top within this of the caster's bottom (either way); a caster taller
// than kFlat (a flat piece casts none).
constexpr float kGap = 10.0f;
// The caster's footprint grown by kMarginBase + kMarginPerHeight x its height (shadows fall away from their caster).
constexpr float kMarginBase = 10.0f;
constexpr float kMarginPerHeight = 1.0f;
// At least this share of the shadow's footprint within the grown footprint, and the shadow at least kMinCover of the
// caster's own footprint in area.
constexpr float kInside = 0.5f;
constexpr float kMinCover = 0.25f;
// Never a shadow by place: a piece wider than this (a room's, a building's shadow).
constexpr float kMaxSpan = 1500.0f;

// Whether a name looks like a shadow's (has "shad" in it, any case).
bool ShadowName(const char* name);
// The names the shadow called `name` would belong to, most specific first (lower case; empty: not a shadow's name,
// or nothing before the suffix): "SkWare_Rail02_Shadow" -> skware_rail02; "BO_HighRail05_Shadow_01" ->
// bo_highrail05_01, bo_highrail0501, bo_highrail05; "LO_forlease03shad" -> lo_forlease03.
std::vector<std::string> OwnerNames(const char* name);

// (b): a piece with that box (as loaded) is a shadow lying under a caster (one of the group's pieces) with that box:
// flat, its top near the caster's bottom, mostly within the caster's footprint grown by its height, covering at least
// kMinCover of that footprint, not wider than kMaxSpan.
bool FallsUnder(V3 shadowMin, V3 shadowMax, V3 groupMin, V3 groupMax);
// And it's that caster's rather than one of `others` (the boxes of the pieces standing on that floor that aren't in the
// group): the shadow's middle no further from the caster's footprint than from any other's (inside both: the nearer
// footprint's middle).
bool NearestToGroup(V3 shadowMin, V3 shadowMax, V3 groupMin, V3 groupMax, const std::vector<std::pair<V3, V3>>& others);
// A copy's shadow named for the copy's source (copies of one piece share their names): its footprint meets the
// caster's grown as in FallsUnder - taken only then.
bool Beside(V3 shadowMin, V3 shadowMax, V3 casterMin, V3 casterMax);
// Whether a piece with that box stands on the floor a shadow with that box lies on (its bottom within kGap of the
// shadow's top) - one of `others` above.
bool StandsBy(V3 pieceMin, V3 shadowMax);

} // namespace Front::LevelEditor::Shadows

namespace Front::LevelEditor::Members {

// Ctrl+click on the scenery selection: what the ray met (nearest first; `passed`: what a plain click passes -
// structure, see-through) and which of those are selected - the index of the piece to drop: the nearest selected one
// no further than what a plain click takes (the first not passed), so a selected shadow or floor in front of it counts
// and a selected piece behind something unselected doesn't. -1: none.
int DropTarget(const std::vector<bool>& passed, const std::vector<bool>& selected);

} // namespace Front::LevelEditor::Members
