#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/front/level_editor.h"

struct ImFont;

namespace Front::LevelEditor::Layout {
struct File;
}

// NOT ORIGINAL - the level editor's problem checker (ours; see level_editor.h - the UX research's #1, after Hammer's
// Check for Problems (Alt+P: Go to / Fix) and TrenchBroom's live issue browser): the Problems window lists what is wrong
// in the edited copy - each row with Go to (the camera there, the thing selected), Fix where the fix is safe, and Ignore
// (this session) - re-checked after each change (a moment after: changes come in bursts) and on demand (Check now). The
// toolbar's Problems button (Alt+P) opens it; it also holds the Budget tab (level_editor_budget.h).
//
// The rules (each row says which; "as shipped" rows - problems the level has without the editor's edits - are hidden
// unless "Show the level's own" is on):
//   Placement - spawns, crowns, flags (and the editor's trigger volumes): probed with the game's collision ray (a CFeeler,
//     static collision; it meets only the FRONT of a face - Nx::RayHitsTriangle 0x0040B420 refuses a determinant below
//     0.03). From the point, lifted kLift units (a spawn standing on the floor is on it, not in it), six directions are
//     probed: going out (the first face facing the point) and coming back in from kReach away (a face facing away from
//     the point, met from outside). A direction is "behind a surface" when a face facing away is nearer than any face
//     facing the point. Then:
//       - inside solid: behind a surface both ways along one axis (closed in, as inside a box or a wall) - no Fix (Go to);
//       - below the ground: behind a surface upwards (under a floor seen from above) - Fix: onto that surface;
//       - nothing below: no face within kGroundReach below (it falls out of the level) - no Fix.
//   Rails - floating rail points: a rail point the editor moved or added with no surface within kRailNear (probed the
//     same way, both ways); rails of one point: a rail node linked to nothing and by nothing (the rail manager makes no
//     segment of it) - the level's own count as shipped.
//   Collision - a moved piece with no collision of its own and none taken along (shared collision found nothing): its
//     collision, if it had any in a shared object, stayed where it was; a piece hidden while the faces it took along
//     are still on (Fix: switched off with it).
//   Limits - any budget meter over the engine's limit (error) or at 90 % of it (warning) - level_editor_budget.h.
//   Triggers - the editor's trigger volumes that do nothing yet (Triggers::Problem), whose centre is inside solid or
//     below the ground; gap starts with no end anywhere (the editor's triggers or a level trigger with the same GapID)
//     and ends with no start; proximity nodes and level triggers whose TriggerScript (or the editor's enter / exit
//     script) isn't a script the game has (Script::ScriptExists 0x00472DE0), proximity nodes without one, a level's
//     trigger faces with no node of their name (the trigger component finds no node - nothing runs).
//   Online parity - in a level shared with THUGPRO (a ported level), each spawn, crown or flag the editor moved, added
//     or hid (THUGPRO players have it as shipped; edited copies never go online, but a bake would change it).
//   Multiplayer - free skate has a Player1 restart; the 2-player / network modes Multiplayer restarts (one only: every
//     player starts there); HORSE a Horse restart; King of the Hill crowns (at most 10); CTF: Red and Blue each with
//     their flag (TRG_CTF_<Colour>), base (TRG_CTF_<Colour>_Base) and team restart (TRG_CTF_Restart_<Colour>) - Green and
//     Yellow complete or not there at all.
//   Layout records - a record of the loaded layout whose thing (a node, a scenery piece, an object, a source) isn't in
//     the level (a level / DLL update, a hand-edited file): logged when the layout goes in, listed here, and KEPT - the
//     next save writes it again (Layout::File::carried) unless Fix (Drop record) drops it, so an update that brings
//     the thing back finds its edit again. Never silently dropped.
namespace Front::LevelEditor::Checks {

// --- the pure parts (self-tested: src/hook/selftests_editor_ux5.cpp) -------------------------------------------------

enum class Severity : uint8_t { Error, Warning, Info };
const char* SeverityName(Severity severity);
enum class Rule : uint8_t {
    InsideSolid,
    BelowGround,
    NoGround,
    FloatingRail,
    LonelyRail,
    CollisionLeft,
    HiddenCollision,
    Limit,
    TriggerIdle,
    TriggerScript,
    GapUnpaired,
    OnlineParity,
    Multiplayer,
    LayoutRecord,
};
constexpr int kRules = 14;
const char* RuleName(Rule rule); // "Inside solid"...

struct Issue {
    Rule rule = Rule::Limit;
    Severity severity = Severity::Warning;
    std::string text;       // what is wrong
    uint32_t node = 0;      // the node it is about (Go to selects it; 0 none)
    uint32_t sector = 0;    // or the scenery piece
    bool hasPos = false;    // or a place to look at
    V3 pos = {};
    bool fixable = false;   // a safe Fix is offered
    V3 fixPos = {};         // (placement: where Fix puts the node)
    bool levelOwn = false;  // the level has it as shipped (not the editor's doing)
    std::string record;     // (layout records: the record's line)
    uint32_t key = 0;       // identity (Ignore): the rule, the subject and the detail
};
// An issue's identity from its rule, subject (a node, a sector, 0) and a detail text.
uint32_t IssueKey(Rule rule, uint32_t subject, const std::string& detail);
// Sorted for the list: errors first, then warnings, infos; the editor's before the level's own; then by rule, text.
void Sort(std::vector<Issue>* issues);

// Placement probes (see the top). Distances from the point; -1: nothing met.
constexpr float kLift = 12.0f, kReach = 2000.0f, kSideReach = 150.0f, kGroundReach = 5000.0f, kRailNear = 30.0f;
struct Probe {
    float out = -1.0f; // the nearest face facing the point, going out
    float in = -1.0f;  // the nearest face facing away (met coming back in from the reach)
};
// Whether a direction is behind a surface: a face facing away nearer than any facing the point.
bool Behind(const Probe& probe);
// The six directions in order: +x, -x, +y, -y, +z, -z.
enum class Place : uint8_t { Clear, InsideSolid, BelowGround };
Place Judge(const Probe probes[6]);
// A rail point floats when no probe met a face within `near` either way.
bool Floating(const Probe probes[6], float near);

// Rails of one point: given each rail node's links (indices into the same list), those linked to nothing and by
// nothing.
std::vector<int> LonelyPoints(const std::vector<std::vector<int>>& links);

// Multiplayer's needs (see the top). Teams: Red, Blue, Green, Yellow.
struct TeamNodes {
    bool flag = false, base = false, restart = false;
};
struct MpCounts {
    int player1 = 0, multiplayer = 0, horse = 0, crowns = 0;
    TeamNodes teams[4];
};
struct MpProblem {
    Severity severity;
    std::string text;
    int team = -1; // (CTF: the team, -1 none)
};
std::vector<MpProblem> MultiplayerProblems(const MpCounts& counts);

// Layout records: one "kind key" (the first two words - three for lock / order) of a record's line, the identity a
// carried record is matched by (a record of that kind and key captured again: not carried).
std::string RecordKey(const std::string& line);
// The carried lines (`unresolved`) not matched by any line of `captured` (a layout's text).
std::vector<std::string> CarriedLines(const std::vector<std::string>& unresolved, const std::string& captured);

// --- the game side ------------------------------------------------------------------------------------------------

// The game's collision ray (static collision): the first face met from `from` to `to` (false: none) - where, its
// normal, its collision object's checksum (0: none).
bool Cast(V3 from, V3 to, V3* hit, V3* normal = nullptr, uint32_t* sector = nullptr);
// The probes around a point (see the top): `reach` along each direction (ProbeEach: each its own).
void ProbeAround(V3 point, float reach, Probe probes[6]);
void ProbeEach(V3 point, const float reach[6], Probe probes[6]);

// The layout put in place (Layout's Apply, after it): its records that don't resolve to anything in the level - logged
// and listed; their lines kept for the next save (`CarriedRecords`) unless dropped. A new level / Reset level: none.
void NoteLayoutApplied(const Layout::File& file);
void ForgetLayoutRecords();
// The lines to write again (Layout's Capture: `captured` its text so far - lines it has of a record's kind and key
// aren't repeated).
std::vector<std::string> CarriedRecords(const std::string& captured);
// The unresolved records now (the panel's, the screenshot sidecar's).
const std::vector<Issue>& LayoutIssues();

// The editor's frame (Draw, after the model): `changes` (the editor's change count) and `marks` (the Outliner's - what
// the editor changed, by name) - checked again a moment after they change.
void NoteModel(int changes, const std::unordered_map<uint32_t, uint8_t>& marks);
// Once a frame while the editor runs (its Update): the UI's actions (Go to, Fix...).
void Update();
// A new level: the ignored list and the issues forgotten.
void ForgetLevel();
// The issues of the last check, and how many of each severity are shown (not ignored, not the level's own unless
// shown) - the toolbar's button.
const std::vector<Issue>& Issues();
void ShownCounts(int* errors, int* warnings, int* infos);
// The Problems window: open on a tab (0 problems, 1 budget), toggled (Alt+P), the toolbar's button, the window.
constexpr int kProblemsTab = 0, kBudgetTab = 1;
void OpenWindow(int tab);
void ToggleWindow();
void ToolbarButton(float scale);
void Window(float viewX, float viewY, float viewWidth, float viewHeight, float scale, ImFont* mono);

} // namespace Front::LevelEditor::Checks
