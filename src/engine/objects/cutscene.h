#pragma once

// Cutscenes: a cutscene's details object (CinematicEntry, one of the movie manager's movie kinds, vtable
// 0x00647920) loads the cutscene's data (CinematicTable: the
// objects, animations and camera of a .cut file) and plays it, with its subtitles and the skip button.
// 0x004638B0-0x004644A0 and the details' small virtuals here; the rest (loading, the object updates, the camera,
// 0x00463AA0-0x004680B0) is still in the game.

#include <memory>
#include <span>
#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/mem.h"

namespace script {
class Array;
class Struct;
} // namespace script
namespace gfx {
class FaceSkin;
class JointClipFrames;
class ModelLook;
} // namespace gfx
namespace render {
struct FrameSource;
} // namespace render
#include "engine/file/asset_bundle.h"
#include "engine/maths/vec4.h"
#include "engine/objects/node_objects.h"
#include <gsl/pointers>

namespace objects {

class GameEntity;

// 0x00465510 (stdcall): a cutscene's binary objects table (version, count, then {name, model, skeleton, anim,
// flags} each) as `params`' 0x40E32C3C and array 0x4DE5330C. True.
bool objects_from_table(script::Struct* params, const uint32_t* table);
// 0x004645A0 / 0x004645E0: the animation sources' pools (0x006FA7F4).
void* anim_source_allocate(unsigned int size);
void anim_source_free(void* item);

// 0x00463AA0: a matrix from a rotation (quaternion: its x, y and z negated in place - the original's) and a
// translation (w 1).
void quat_to_matrix(std::span<float, 4> quat, std::span<const float, 4> translation, std::span<float, 16> matrix);
// The skater appearance's face (its +0x54). No address of its own: the original reads it through the getter folded at
// 0x00576E60 (modules::Viewer::get_viewer_entity).
gfx::FaceSkin* appearance_face(gfx::ModelLook* appearance);
// A model's fader (its +0x2C). No address of its own: the original reads it through the getter folded at 0x00532EB0
// (online::Hub::get_matchmaking_client).
void* model_fader(void* model);
// What a cutscene is told about the skater it shows (the original's three words: an object camera's or a skater
// camera's context, 0x00466490 / 0x004678A0): the first profile's appearance and its details structure (the profile's
// +0x5C), and whether it's a skater cutscene (its own heaps, music and camera; the original kept the word and read its
// low byte).
struct CutsceneSkater {
    gfx::ModelLook* appearance{nullptr};
    script::Struct* details{nullptr};
    bool skater{false};
};

// 0x00464340 (code Ghidra missed; nothing calls it): an identity rotation (0, 0, 0, 1).
struct IdentityQuat {
    std::array<float, 4> q;
};

void assign_value(IdentityQuat& quat);
// 0x00463BA0: the name of the skater model a cutscene uses: the profile 0x1804A7C4 names looked up in array
// 0xCA1DB456, else "Male1" when integer 0x3F813177 is 1, "Female1" when not.
const char* skater_model_name(script::Struct* params);
// 0x00463DA0: a colour from the structure `name` in `params` ({r, g, b} in percent unless 0x5A96985D is set);
// white otherwise. Returns `out`.
std::span<float, 4> read_colour(std::span<float, 4> out, script::Struct* params, uint32_t name);
// 0x00463E80 / 0x00463ED0: the lights of the nodes named in array 0x2A79C44E added (0x00490570: a light from a
// node's colour and radii) / taken away again (0x0048FCA0, by node name).
void add_cutscene_lights();
void remove_cutscene_lights();
// 0x004640D0: the cutscene heaps set up (the main top-down heap current; 0x25800 bytes asked of 0x006FA7C0's
// object; all but 0x32000 of what's free).
void set_up_cutscene_heaps();

// 0x004638B0: the file name of `path` without its folders or extension, in a static buffer
// (0x006F2160). A path without a '\' or '/' isn't copied - the buffer keeps the previous name (an original quirk)
// - but the extension is still cut from what's there.
const char* cutscene_name_from_path(const char* path);

// (The original's vtable 0x00647908 holds only its destructor; the offsets below are the original's, which has the
// vtable at +0x00.)
class CinematicTable : public memory::HeapObject {
public:
    // 0x00463910: the checksum of the cutscene's name followed by `suffix` and, unless it's -1, `number` and a
    // newline ("%s%d\n" - the newline is part of the original's format).
    uint32_t hash_of_name(const char* suffix, int number);
    // 0x004639A0: whether the cutscene has reached its end (1 / 0).
    int is_finished();
    // 0x004639C0: the vblanks since the last call added to the cutscene's own count.
    void update_vblanks();
    // 0x00463A10: the subtitles (an array of structures {time, ...}, each run through script 0x625985F9 when its
    // time comes); the first one's time is fetched now.
    void set_subtitles(script::Array* subtitles);
    // 0x00464120: the skip button: when skipping is allowed (and the global 0xA2107A0E isn't set), pressing X
    // (0x40) or Start (0x800) on the first controller jumps to the end.
    void check_skip();
    // 0x004644A0: the next subtitle, once the cutscene's time reaches it (its duration from 0x79A07F3F).
    void update_subtitles();

    // 0x00463C70: the cutscene's objects dropped; the objects they stood in for get back what they lent (their
    // models' settings) and the first one's position (lent_pos_: every one of them is put there).
    void destroy_objects();
    // 0x00463F20: an asset loaded (0x00408060) and listed with the cutscene's.
    void load_asset(uint32_t name, void* data, uint32_t size);
    // 0x00463F70: the same for an animation, also kept in the animation list (with what 0x0048CF50 finds for it).
    void load_animation(uint32_t name, void* data, uint32_t size);
    // 0x00464000: a composite object for the cutscene (type 0xCDEB9229) made from `params` by scripts 0x1F29C5A5
    // and 0x2E20EA32.
    gsl::owner<GameEntity*> create_object(script::Struct* params);
    // 0x004641A0: (when +0x34B0) the objects listed in 0x67F08671 shown again.
    void show_hidden_objects();
    // 0x00464220: (when +0x34B0) the camera placed by the cutscene's camera animation at its time; the objects'
    // animations updated unless skipped.
    void refresh_viewer(void* camera);
    // 0x00464360: each object's skeleton posed by its animation at the cutscene's time.
    void update_object_poses();
    // 0x00464640: nothing loaded, a new parameters structure.
    CinematicTable();
    // 0x00466C20 (0x00468080 the deleting one): the bundle, the animation sources, the objects, the lights and every
    // asset dropped; the cutscene's .qb unloaded.
    ~CinematicTable();
    CinematicTable(const CinematicTable&) = delete;
    CinematicTable& operator=(const CinematicTable&) = delete;
    // 0x00464780: (when +0x34B0 and the global integer 0x4AF50A5B is set) script 0x2E20EA32 run again on every
    // object with a model and the skater; the global cleared.
    void refresh_objects();
    // 0x004648D0: the objects of `params`' array 0x4DE5330C made (their models, skeletons, animations; the
    // skater's from its profile and appearance), linked to their parents and the level objects they stand in for.
    void add_objects(script::Struct* params);

    // An animation it loaded (12 bytes).
    struct AnimationEntry {
        uint32_t name;
        render::FrameSource* source; // +0x04: an animation source (0x214 bytes, made when an object uses it)
        void* data;   // +0x08
    };
    // An object of the cutscene (0x20 bytes).
    struct ObjectEntry {
        uint32_t name;     // +0x00: its id (a child's is its parent's + 1)
        uint32_t skeleton; // +0x04
        int part;          // +0x08: the animation part it plays (its animation: name + part * 10)
        bool active;       // +0x0C: it stands in for an object of the level
        std::array<uint8_t, 3> padd_;
        AnimationEntry* animation; // +0x10
        GameEntity* object;        // +0x14
        GameEntity* parent;        // +0x18: the object of the entry named name - 1
        int anim_index_;           // +0x1C: its index in the object animations (-1: none)
    };

    ObjectEntry* find_entry(uint32_t name);
    void copy_skeleton(ObjectEntry& from, ObjectEntry& to);
    void apply_skater_look(ObjectEntry& entry);
    // 0x004656E0: the cutscene's clock (from the frame counter when 0x0C00425A is set, a fixed step when +0x3474,
    // else the vblanks); with +0x34B0, past the camera animation's end the next camera / object animation parts
    // and every object's next animation part.
    void update_time();
    // 0x00465AB0: a cutscene object's model shown; a stand-in fades out the level object it replaces and moves it
    // along. The sound component (0x208353AC) gets the object's matrix.
    void update_stand_in(GameEntity* object);
    // 0x00465C00: the object animation applied: the level's animated objects, then the cutscene's (children hang
    // from their parents' bone 0x5A0E0860).
    void update_object_transforms();
    // 0x00466450: (without +0x34B0) the objects made again from the parameters, the clock back to 0.
    void restart_objects();
    // 0x00467710: the objects whose models the bundle holds keep their skeletons (0x0CBEC8AC); then (+0x34B0) the
    // objects made.
    void mark_loaded_models();
    // 0x004677F0: the bundle taken in (on the cutscene's or the bottom-up heap), the objects and lights made, the
    // bundle dropped; a skater cutscene waits for its music stream. True.
    bool finish_loading(int unused);
    // 0x004678A0: the cutscene file `path` loaded (asynchronously: `a2` passed on) with the settings {appearance,
    // profile, skater cutscene}; its music and heaps set up first. True.
    bool read_in(const char* path, int a2, int unused, const CutsceneSkater* settings);
    // 0x00467B50: whether the file has loaded (a skater cutscene's finished loading the first time it has).
    bool is_loaded();
    // 0x00467BA0: a frame of the cutscene: the clock, skipping, the camera, subtitles, poses and transforms (and
    // the debug time / screenshots).
    void tick_frame(void* camera, script::Struct* params);
    // 0x00467EA0: Update once the music is ready (the objects refreshed first).
    void perform(void* camera, script::Struct* params);
    // 0x00466DD0: the bundle's assets taken in by type (parameters, objects, .qb, camera / object / plain
    // animations, images, models with their textures - a skater's skin only when it's the skater's); the end time.
    void load_from_bundle();
    void ingest_mesh(int index, file::AssetBundle::TableRow* entry, void* data);

    // The fields other code reads and sets.
    [[nodiscard]] bool ready() const { return ready_; }
    [[nodiscard]] bool can_skip() const { return can_skip_; }
    void set_unknown3474(uint8_t value) { unknown3474_ = value; }
    float& clock() { return clock_; }
    [[nodiscard]] float clock() const { return clock_; }
    void set_clock(float value) { clock_ = value; }
    void set_loop_start(float value) { loop_start_ = value; }
    void set_unknown348c(float value) { unknown348c_ = value; }
    [[nodiscard]] uint8_t flag_34b_0() const { return flag_34b_0_; }

private:
    uint32_t unknown4_{};
    std::unique_ptr<file::AssetBundle> bundle_ptr_; // +0x08: the cutscene's loaded bundle
    bool ready_flag_{};                             // +0x0C
    std::array<uint8_t, 3> unknownd_{};
    int camera_parts_{0};          // +0x10: the camera animation's parts (after the first)
    gsl::owner<render::FrameSource*> camera_source_{nullptr}; // +0x14: its animation source
    int object_parts_{0};          // +0x18: the object animation's parts
    gsl::owner<render::FrameSource*> object_source_{nullptr}; // +0x1C
    gfx::JointClipFrames* object_anims_{};  // +0x20
    gfx::JointClipFrames* camera_anim_{};   // +0x24
    gfx::JointClipFrames* anim_list_{};     // +0x28: the object animations (their bones the objects' names)
    maths::Vec4 lent_sphere_{}; // +0x2C: the bounding sphere given back to the objects' models
    uint8_t lent_3c_{};       // +0x3C
    uint8_t lent_3d_{};       // +0x3D
    std::array<uint8_t, 2> unknown3e_{};
    maths::Vec4 lent_pos_{}; // +0x40: the position of the first level object a cutscene object stood in for (given
                             // back to every one of them afterwards, as the original)
    bool lent_saved_{false}; // +0x50: it (and its model's settings, +0x2C-0x3D) saved
    std::array<uint8_t, 3> unknown51_{};
    std::array<ObjectEntry, 256> entities_{};    // +0x54
    int object_count_{0};            // +0x2054
    std::array<AnimationEntry, 256> animations_{}; // +0x2058
    int animation_count_{};            // +0x2C58
    std::array<uint32_t, 512> assets_{};           // +0x2C5C
    int asset_count_{0};               // +0x345C
    bool ready_{false};                // +0x3460: the music started (or none to wait for)
    bool can_skip_{};                // +0x3461
    bool skipped_{};                 // +0x3462
    uint8_t unknown3463_{};
    int camera_part_{0};      // +0x3464
    int object_part_{0};      // +0x3468
    float end_time_{0.0f};    // +0x346C: seconds
    float unknown3470_{1.0f}; // +0x3470 (1 at first)
    uint8_t unknown3474_{0};
    std::array<uint8_t, 3> unknown3475_{};
    float previous_clock_{0.0f}; // +0x3478: the clock before this frame's step (events fire between the two)
    float clock_{0.0f};      // +0x347C: seconds into the cutscene
    float unknown3480_{0.0f};  // +0x3480
    float camera_start_{0.0f}; // +0x3484
    float loop_start_{0.0f}; // where a looping cutscene goes back to (LoopLength)
    float unknown348c_{0.0f};   // +0x348C: where a fixed-step cutscene loops (0: never)
    uint64_t paused_vblanks_{}; // +0x3490
    uint64_t vblanks_{0};       // +0x3498: counted by update_vblanks
    uint64_t prev_retraces_{0}; // +0x34A0: the timer's count at the last update_vblanks
    gfx::ModelLook* skater_appearance_{nullptr}; // +0x34A8: the skater's appearance
    script::Struct* skater_details_{nullptr};    // +0x34AC: the skater's profile structure
    uint8_t flag_34b_0_{};    // +0x34B0: a skater cutscene with its own heaps, music, camera (kept as given)
    std::array<uint8_t, 3> unknown34b1_{};
    uint32_t label_{};          // +0x34B4: the cutscene's name checksum
    script::Array* subtitles_{nullptr}; // +0x34B8
    float subtitle_duration_{}; // +0x34BC: the last subtitle's (0 if it gave none)
    float subtitle_time_{0.0f}; // +0x34C0: when the last subtitle started
    float next_subtitle_time_{-1.0f}; // +0x34C4: -1: no more
    uint32_t next_subtitle_{0};       // +0x34C8: its index
    script::Struct* call_args_; // +0x34CC
};

// The cutscene kind of the movie manager's movies (vtable 0x00647920).
class CinematicEntry final : public VideoEntry {
public:
    CinematicEntry(const CinematicEntry&) = delete;
    CinematicEntry& operator=(const CinematicEntry&) = delete;
    // 0x00466490: the movie's constructor, then the cutscene's: no data, a parameters structure, the three settings
    // (the third's low byte: whether it owns its data).
    explicit CinematicEntry(const CutsceneSkater* settings);
    // 0x00466560 (0x00467F30 the deleting one): the parameters freed; when it owns its data, the data's asset unloaded
    // (0x00407B90 / 0x00407B20) and 0x0048E970 / 0x0048E990 given back the three values; then the movie's.
    ~CinematicEntry() override;

    // 0x00466620 (slot 1): the cutscene named by 0xA1DC81F9 set up: an asset found by name, or (when it owns its
    // data) its bundle loaded with the loading screen up and the music muted (start time / loop length in
    // milliseconds, subtitles from <name>_subtitles). Whether it's there.
    bool read_in(script::Struct* params) override;
    // 0x00467F60 (slot 2): once loaded: the begin script (once), then the cutscene played - or, with +0x5D, only its
    // clock and objects; +0x60 counts down to a call of slot 12.
    void tick_frame() override;
    // Slot 3 (the shared "true").
    bool kickoff() override { return true; }
    // 0x00468040 (slot 5): done (the movie's flag, or the cutscene loaded and at its end).
    bool is_finished() override;
    // Slot 6 (the shared "false").
    bool is_held_and_complete() override { return false; }
    // 0x004680B0 (slot 7): a skater cutscene loaded with its music started - its camera is the one to show.
    bool has_camera() override;
    // 0x00466A10 (slot 8): the cutscene over: its objects (or its asset, heaps, the music...) dropped and its end
    // scripts run.
    void stop() override;
    // 0x00464580 (slot 9): whether the loaded cutscene may be skipped (false if none is loaded).
    bool can_be_skipped() override;
    // 0x00463A50 (slot 11): slot 12 with whether +0x5D is clear.
    void push_option_state() override;
    // 0x00463A60 (slot 13): while +0x60 is -1, slot 12 with 0 if +0x5D is set; else with 1, then +0x60 takes the
    // global integer 0xA2A4F02F.
    void update_option_state() override;

private:
    CinematicTable* content_{nullptr}; // +0x2C
    uint32_t asset_{0};                // +0x30
    uint32_t begin_script_{}; // +0x34: the cutscene's name + "_begin"
    bool begun_{};            // +0x38: the begin script run
    std::array<uint8_t, 3> unknown39_{};
    uint32_t end_script_{}; // +0x3C: + "_end"
    script::Struct* args_; // +0x40
    float music_volume_{}; // +0x44: the volumes before the cutscene (given back at its end)
    float stream_volume0_{}; // +0x48
    float stream_volume1_{}; // +0x4C
    CutsceneSkater skater_{}; // +0x50
    uint8_t owns_data_;    // +0x5C (the skater flag, as it is)
    bool flag_5d_{false};  // +0x5D
    std::array<uint8_t, 2> unknown5e_{};
    int unknown60_{-1}; // +0x60
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(CinematicEntry, content_) == 0x2C && offsetof(CinematicEntry, flag_5d_) == 0x5D &&
                          offsetof(CinematicEntry, unknown60_) == 0x60,
                      "layout must match the original");
    }
};

static_assert(sizeof(CinematicTable::ObjectEntry) == 0x20, "layout must match the original");

} // namespace objects
