#pragma once

#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_ui.h"

struct ImFont;

// NOT ORIGINAL - the level editor's entities (ours; see level_editor.h - the feature map's second round): the level's
// living and moving things made, changed and previewed - pedestrians and vehicles, bouncy (rigid-body) objects, moving
// objects, sound emitters, particle systems - and its objects that aren't created at the start shown as ghosts.
//
//   - Peds and cars (Pedestrian / Vehicle nodes): new ones are copies of a ped's / car's node (its skeleton, model,
//     anim set, profile, AI type and TriggerScript - the source's behaviour), made as the editor's object copies are
//     (level_editor_objects.h: the node copied into the copies' node array, then 0x005A2E50 CreateFromStructure as the
//     level's load makes it) - so only types the level has made (their assets are loaded). The Place palette lists one
//     entry per type the level has (class, Type, profile, model); a copy follows its source's path (its links= are
//     names) until the Inspector's Path section assigns another (level_editor_chains.h - the copy is made again from
//     its node). Saved as `copy` records; deleted with Delete; undone as copies are.
//   - Model swap: an object's model= set to another model a live object of the level has (so it's loaded) - a field
//     edit (level_editor_fields.h: undo, Home, the layout's `set ... f.model=` and the edited copy's early field pass:
//     on reload the level makes it with that model). Live: the object is made again from its node where it stands
//     (killed; once the game has removed it - its object update, so at the latest on leaving the editor - made from
//     its node and put back where it was, turned and scaled as it was).
//   - Show uncreated (the Inspector's View): object nodes (LevelObject, GameObject, Pedestrian, Vehicle) the level
//     hasn't made - not CreatedAtStart, made later by a script's `create` - drawn as ghosts (a box and the name). Their
//     nodes move as point nodes (level_editor_node_edit.h: pos= / angles=, `set` records): a script's `create` makes
//     them where they were put.
//   - Bouncy objects (the node flag `bouncy`: a rigid body - contacts, center_of_mass, coeff_restitution,
//     coeff_friction, skater_collision_*, SoundType): the Inspector's Physics section edits those fields (field edits,
//     on reload); the Place palette's "Bouncy set" entries copy a set - the bouncy objects whose names share a stem
//     (BO_Bouncy_Ball01..04) - placed as the level placed them, about where the cursor points.
//   - Moving objects (a node whose links= name a Waypoint: peds, cars, moving game objects): a path speed (mph) set
//     with the game's own Obj_SetPathVelocity on the object (RunScript of the member function: live, again whenever
//     the object is made again - a level script that sets its own speed later wins), `speed` records; and a preview
//     while the game is paused: Play / Pause / Step move the object along its path (from its node's first link, along
//     each waypoint's first link) at that speed - no script runs, nothing is saved; Stop (and any change, selection,
//     save or leaving) puts it back.
//   - Sound emitters (EmitterObject): a sound for an emitter - one of the sounds the level has loaded (the level's wave
//     table, 0x006F5E68) - played by this module as THUG2's own emitter sounds sound (a sound played "from" an emitter
//     sounds from the shape's nearest point to the listener: EmitterNode::GetClosestPoint, the sound's own dropoff,
//     CSfxManager::GetVolume), again whenever it stops (a loop keeps going; a one-shot repeats) while the listener is
//     in range, volume in percent; a sphere emitter's radius set (its node's Radius and the emitter manager's copy).
//     The Place palette's "Sound emitter: <sound>" entries add an emitter (level_editor_node_create.h) with that sound.
//     `emitter` records. Silent while the game is paused (the editor).
//   - Particle systems (ParticleObject): moved and turned as point nodes (pos= / angles=, `set` records) - their
//     emitter's MidPosition and EndPosition (world places, as the level's particle nodes have them) moved and turned
//     with them, its BoxDims* boxes turned (map4: TurnParticle), and the system made again
//     there (killed; once the game has removed it, made from its node: shown at the latest on leaving the editor). New
//     ones are copies of a level's particle node (`copy` records, their Mid / End moved with them).
// Layout records (version 2 still; an older reader skips them):
//   speed <node> <mph>                                   a moving object's path speed
//   emitter <node> sound=<checksum> vol=<percent> radius=<r>  an emitter's sound (sound=0: none), its radius (0: as is)
// tools/game/apply_layout.ts bakes them as node fields only this project's DLL reads - ed_path_speed (mph), ed_sound /
// ed_volume - and the radius as the node's Radius (warned); the DLL reads those fields when a level loads, too.
// Offline only: layouts apply only offline (level_editor_layout.h); the baked fields are ignored in a network session.
namespace Front::LevelEditor::Entities {

// --- the pure parts (self-tested: src/hook/selftests_editor_entities.cpp) -----------------------------------------

// A bouncy set's stem: the name with its trailing digits, then trailing '_', taken off ("BO_Bouncy_Ball01" ->
// "BO_Bouncy_Ball"); the name itself when that would leave nothing.
std::string SetStem(const std::string& name);
// The point a set is placed about: its members' middle across (x, z) and its lowest y.
V3 SetAnchor(const std::vector<V3>& places);

// A path (waypoints in order; `closed`: the last links back to the first): its length, and the place `distance` along
// it (wrapping past the end: back to the start) with the direction it heads there. False: fewer than 2 points or no
// length.
float PathLength(const std::vector<V3>& points, bool closed);
bool PathSample(const std::vector<V3>& points, bool closed, float distance, V3* pos, V3* dir);
// Miles an hour in the game's units (inches) a second: 1 mph = 17.6 in/s.
inline float MphToUnits(float mph) { return mph * 17.6f; }
// The limits the editor accepts (the Inspector's fields clamp to them; a layout record outside them is refused).
constexpr float kMinMph = 0.1f, kMaxMph = 200.0f;
constexpr float kMinVolume = 0.0f, kMaxVolume = 400.0f;
constexpr float kMinRadius = 1.0f, kMaxRadius = 100000.0f;

// (map4) A particle system's emitter as its node places it: MidPosition / EndPosition (world places) and the three
// boxes the particles start / pass / end in (BoxDimsStart / BoxDimsMid / BoxDimsEnd - extents along the world's axes:
// the engine bounds the system with place +- half the box on each world axis, 0x00495260). Turned with its node by
// `turn` radians about the up axis (as TurnAboutUp turns: a node's heading) and moved to `pos`, from `origin` (the
// node as it was at `from`, never the last result - no drift): Mid / End turned about the node's place, each box the
// box along the world's axes that holds the turned box (|cos| x + |sin| z, y kept - a quarter turn swaps x and z).
struct ParticleEmitter {
    bool hasMid = false, hasEnd = false;
    V3 mid = {}, end = {};
    bool hasBox[3] = {};
    V3 box[3] = {}; // start, mid, end
};
ParticleEmitter TurnParticle(const ParticleEmitter& origin, V3 from, V3 pos, float turn);
V3 TurnBoxExtents(V3 extents, float turn);

// A type for the Place palette: peds / cars of the same class, Type, profile and model are one entry (the first).
std::string TypeKey(uint32_t cls, uint32_t type, uint32_t profile, const std::string& model);

// What the layout keeps.
struct Speed {
    uint32_t name = 0;
    float mph = 0.0f;
};
struct EmitterSound {
    uint32_t name = 0;
    uint32_t sound = 0;     // (0: none)
    float volume = 100.0f;  // percent
    float radius = 0.0f;    // a sphere's radius (0: the node's own)
};
struct State {
    std::vector<Speed> speeds;
    std::vector<EmitterSound> emitters;
};
bool SameState(const State& a, const State& b);
bool Empty(const State& state);
// The entry for `name` (null: none).
const Speed* SpeedOf(const State& state, uint32_t name);
const EmitterSound* EmitterOf(const State& state, uint32_t name);
// Set (replaced) / taken out (mph 0, an emitter with no sound and no radius: taken out).
void SetSpeed(State* state, uint32_t name, float mph);
void SetEmitter(State* state, const EmitterSound& emitter);
// The records' text, and one read (`kind` the record's first word; false: malformed - a value out of range, a node
// twice, a repeated or unknown-format key; unknown keys are skipped).
std::string WriteRecords(const State& state);
bool IsRecord(const std::string& kind);
bool ReadRecord(const std::string& kind, std::istringstream& in, State* state);

// --- the game side ------------------------------------------------------------------------------------------------

// Once a frame (the editor's Update, on or off): the emitters' sounds, the speeds kept on their objects, objects made
// again, the preview; `selected` the editor's selected object (0: none) - the preview stops when it changes;
// `dragging` the editor is moving something.
void Tick(bool editorActive, uint32_t selected, bool dragging);
// The editor started / stopped (the preview stopped).
void Start();
void Stop();

// The state (layouts, undo).
State Current();
// Put in place: speeds applied to their objects, emitters' sounds and radii (those no longer named: as the level has
// them).
void SetState(const State& state);
// A change made from the Inspector since the last call: the state before it (the editor pushes it as one undo step).
bool TakeUndo(State* before);

// A node the node edits moved or turned (NodeEdit::Set): a particle system's emitter places and boxes moved and turned
// with it (TurnParticle, from the node as the level - or the copy - had it), the system made again.
void NodeMoved(uint32_t name, V3 pos, V3 angles);
// Whether the node edits move this node: a particle node, an object node the level hasn't made (a ghost).
bool MovableNode(uint32_t name, int family);
// The Inspector's note for those.
const char* WhenApplied(uint32_t name, int family);

// A layout's field edit gave object `id` its model= (Layout::Apply): a copy (made from its source's node first) is made
// again with it.
void ModelEdited(uint32_t id);
// The preview stopped (the object put back) - before anything reads or saves the object's place.
void StopPreview();
// The previewed object put back for a moment (a layout capture reads where it stands) and placed on its path again.
void PreviewSuspend();
void PreviewResume();

// The Place palette's entries after the editor's own: peds, cars, bouncy sets, particle systems, sound emitters.
struct PaletteEntry {
    std::string label;
    uint32_t colour = 0xFFC0C0C0;
    std::string why; // (empty: can be placed now)
};
const std::vector<PaletteEntry>& Palette();
// Made again at the next Palette() (the palette opened: objects made since show up; kept while it's open - an armed
// entry keeps its index).
void RefreshPalette();
// Entry `index` placed at `at` (the ground under the cursor), facing `yaw`: the copies made (their names; Objects'
// copies - the editor's undo step), or a node to add (an emitter: `node` true, `spec` filled by AddEmitterSpec).
struct Placed {
    std::vector<uint32_t> copies;
    bool emitter = false;
    uint32_t sound = 0;
    std::string message;
};
Placed Place(int index, V3 at, float yaw);
// An emitter added for a "Sound emitter: <sound>" entry: its sound set (after the node was made).
void AttachSound(uint32_t emitter, uint32_t sound);

// The view: objects the level hasn't made shown as ghosts (the Inspector's View: "Show uncreated objects").
bool& ShowUncreated();
// The ghosts and the previewed path drawn over the view (back buffer pixels).
void AddOverlay(V3 camera, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                std::vector<Ui::Line>* lines, std::vector<Ui::Overlay::Text>* texts);

// The UI (level_editor_entities_ui.cpp, ImGui): the Inspector's sections for the selected node (`node`: Nodes::All()
// index, -1 none) and object (`object`: its id, 0 none) - model, physics, moving, sound, particle; field edits go to
// `requests` (level_editor_ui.h's kFieldSet); the rest is queued for Tick.
void InspectorSection(int node, uint32_t object, ImFont* mono, std::vector<Ui::Request>* requests);
// The View section's checkbox (Show uncreated objects).
void ViewOptions();
// What the UI asks for, done in the next Tick.
void Queue(std::function<void()> action);

// A new level: forgotten (the sounds stopped, the baked fields read again once the level is up).
void ForgetLevel();

// For the UI (level_editor_entities_ui.cpp): what it shows and changes.
namespace Detail {
const std::vector<std::string>& Models(); // the models the level's live objects have (model=)
void WatchModel(uint32_t id, const std::string& made); // a swap asked for: `made` the model it has now
bool HasPath(uint32_t id);                // its node's first link starts a waypoint path
float SpeedSet(uint32_t id);              // its path speed set (0: none)
float DefaultMph(uint32_t id);            // the preview's speed
void ChangeSpeed(uint32_t id, float mph); // (0: none)
EmitterSound EmitterNow(uint32_t name);
void ChangeEmitter(const EmitterSound& e);
float EmitterRadius(uint32_t name, bool* sphere);
std::vector<std::pair<uint32_t, std::string>> Sounds(); // the level's loaded sounds: checksum, label
void SetUiActive(bool active);            // a field of the section is being dragged (the undo step waits)
bool Previewing(uint32_t id);
bool PreviewPlaying();
void PreviewPlay(uint32_t id);            // start / pause / go on
void PreviewStep(uint32_t id);            // half a second along, paused
float PreviewDistance();
float PreviewLength();
} // namespace Detail

} // namespace Front::LevelEditor::Entities
