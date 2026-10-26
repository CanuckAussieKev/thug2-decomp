#pragma once

// Cutscenes (names after THUG's Obj::CCutsceneDetails / Nx::CCutsceneData): a cutscene's details object (one of the
// movie manager's movie kinds, vtable 0x00647920) loads the cutscene's data (CCutsceneData, ~0x34D0 bytes: the
// objects, animations and camera of a .cut file) and plays it, with its subtitles and the skip button.
// 0x004638B0-0x004644A0 and the details' small virtuals here; the rest (loading, the object updates, the camera,
// 0x00463AA0-0x004680B0) is still in the game.

#include <cstddef>
#include <cstdint>

namespace Script {
class CArray;
class CStruct;
}
#include "game/file/asset_bundle.h"

namespace Obj {

class CCompositeObject;

// 0x00465510 (stdcall): a cutscene's binary objects table (version, count, then {name, model, skeleton, anim,
// flags} each) as `params`' 0x40E32C3C and array 0x4DE5330C. True.
bool __stdcall ObjectsFromTable(Script::CStruct* params, const uint32_t* table);
// 0x004645A0 / 0x004645E0: the animation sources' pools (0x006FA7F4).
void* __cdecl AnimSourceAllocate(unsigned int size);
void __cdecl AnimSourceFree(void* item);

// 0x00463AA0: a matrix from a rotation (quaternion: its x, y and z negated in place - the original's) and a
// translation (w 1).
void __cdecl QuatToMatrix(float* quat, const float* translation, float* matrix);
// The skater appearance's face (its +0x54). No address of its own: the original reads it through the getter folded at
// 0x00576E60 (Mdl::Viewer::GetViewerObject). (__fastcall: the appearance in ECX, as the original's call passes it.)
void* __fastcall AppearanceFace(void* appearance);
// A model's fader (its +0x2C). No address of its own: the original reads it through the getter folded at 0x00532EB0
// (GameNet::Manager::GetMatchClient). (__fastcall: the model in ECX.)
void* __fastcall ModelFader(void* model);
// 0x00464340 (code Ghidra missed; nothing calls it): an identity rotation (0, 0, 0, 1).
struct IdentityQuat {
    void Set();
    float q[4];
};
// 0x00463BA0: the name of the skater model a cutscene uses: the profile 0x1804A7C4 names looked up in array
// 0xCA1DB456, else "Male1" when integer 0x3F813177 is 1, "Female1" when not.
const char* __cdecl SkaterModelName(Script::CStruct* params);
// 0x00463DA0: a colour from the structure `name` in `params` ({r, g, b} in percent unless 0x5A96985D is set);
// white otherwise. Returns `out`.
float* __cdecl ReadColour(float* out, Script::CStruct* params, uint32_t name);
// 0x00463E80 / 0x00463ED0: the lights of the nodes named in array 0x2A79C44E added (0x00490570: a light from a
// node's colour and radii) / taken away again (0x0048FCA0, by node name).
void AddCutsceneLights();
void RemoveCutsceneLights();
// 0x004640D0: the cutscene heaps set up (the main top-down heap current; 0x25800 bytes asked of 0x006FA7C0's
// object; all but 0x32000 of what's free).
void SetUpCutsceneHeaps();

// 0x004638B0 (path in EDX): the file name of `path` without its folders or extension, in a static buffer
// (0x006F2160). A path without a '\' or '/' isn't copied - the buffer keeps the previous name (an original quirk)
// - but the extension is still cut from what's there.
const char* CutsceneNameFromPath(const char* path);
void CutsceneNameFromPathStub();

class CCutsceneData {
public:
    // 0x00463910: the checksum of the cutscene's name followed by `suffix` and, unless it's -1, `number` and a
    // newline ("%s%d\n" - the newline is part of the original's format).
    uint32_t NameChecksum(const char* suffix, int number);
    // 0x004639A0: whether the cutscene has reached its end (1 / 0).
    int IsDone();
    // 0x004639C0: the vblanks since the last call added to the cutscene's own count.
    void UpdateVblanks();
    // 0x00463A10: the subtitles (an array of structures {time, ...}, each run through script 0x625985F9 when its
    // time comes); the first one's time is fetched now.
    void SetSubtitles(Script::CArray* subtitles);
    // 0x00464120: the skip button: when skipping is allowed (and the global 0xA2107A0E isn't set), pressing X
    // (0x40) or Start (0x800) on the first controller jumps to the end.
    void CheckSkip();
    // 0x004644A0: the next subtitle, once the cutscene's time reaches it (its duration from 0x79A07F3F).
    void UpdateSubtitles();

    // 0x00463C70: the cutscene's objects dropped; the objects they stood in for get back what they lent (their
    // models' settings) and the cutscene's colour.
    void DestroyObjects();
    // 0x00463F20: an asset loaded (0x00408060) and listed with the cutscene's.
    void LoadAsset(uint32_t name, uint32_t a2, uint32_t a3);
    // 0x00463F70: the same for an animation, also kept in the animation list (with what 0x0048CF50 finds for it).
    void LoadAnimation(uint32_t name, uint32_t a2, uint32_t a3);
    // 0x00464000: a composite object for the cutscene (type 0xCDEB9229) made from `params` by scripts 0x1F29C5A5
    // and 0x2E20EA32.
    CCompositeObject* CreateObject(Script::CStruct* params);
    // 0x004641A0: (when +0x34B0) the objects listed in 0x67F08671 shown again.
    void ShowHiddenObjects();
    // 0x00464220: (when +0x34B0) the camera placed by the cutscene's camera animation at its time; the objects'
    // animations updated unless skipped.
    void UpdateCamera(void* camera);
    // 0x00464360: each object's skeleton posed by its animation at the cutscene's time.
    void UpdateObjectPoses();
    // 0x00464640 (returns this): nothing loaded, a new parameters structure.
    CCutsceneData* Construct();
    // 0x00464780: (when +0x34B0 and the global integer 0x4AF50A5B is set) script 0x2E20EA32 run again on every
    // object with a model and the skater; the global cleared.
    void RefreshObjects();
    // 0x004648D0: the objects of `params`' array 0x4DE5330C made (their models, skeletons, animations; the
    // skater's from its profile and appearance), linked to their parents and the level objects they stand in for.
    void AddObjects(Script::CStruct* params);

    // An animation it loaded (12 bytes).
    struct AnimationEntry {
        uint32_t name;
        void* source;             // +0x04: an animation source (0x214 bytes, made when an object uses it)
        void* data;               // +0x08
    };
    // An object of the cutscene (0x20 bytes).
    struct ObjectEntry {
        uint32_t name;              // +0x00: its id (a child's is its parent's + 1)
        uint32_t skeleton;          // +0x04
        int part;                   // +0x08: the animation part it plays (its animation: name + part * 10)
        bool active;                // +0x0C: it stands in for an object of the level
        uint8_t padD[3];
        AnimationEntry* animation;  // +0x10
        CCompositeObject* object;   // +0x14
        CCompositeObject* parent;   // +0x18: the object of the entry named name - 1
        int animIndex;              // +0x1C: its index in the object animations (-1: none)
    };

    ObjectEntry* FindEntry(uint32_t name);
    void CopySkeleton(ObjectEntry& from, ObjectEntry& to);
    void ApplySkaterLook(ObjectEntry& entry);
    // 0x004656E0: the cutscene's clock (from the frame counter when 0x0C00425A is set, a fixed step when +0x3474,
    // else the vblanks); with +0x34B0, past the camera animation's end the next camera / object animation parts
    // and every object's next animation part.
    void UpdateTime(int unused);
    // 0x00465AB0: a cutscene object's model shown; a stand-in fades out the level object it replaces and moves it
    // along. The sound component (0x208353AC) gets the object's matrix.
    void UpdateStandIn(CCompositeObject* object);
    // 0x00465C00: the object animation applied: the level's animated objects, then the cutscene's (children hang
    // from their parents' bone 0x5A0E0860).
    void UpdateObjectTransforms();
    // 0x00466450: (without +0x34B0) the objects made again from the parameters, the clock back to 0.
    void RestartObjects();
    // 0x00467710: the objects whose models the bundle holds keep their skeletons (0x0CBEC8AC); then (+0x34B0) the
    // objects made.
    void MarkLoadedModels();
    // 0x004677F0: the bundle taken in (on the cutscene's or the bottom-up heap), the objects and lights made, the
    // bundle dropped; a skater cutscene waits for its music stream. True.
    bool FinishLoading(int unused);
    // 0x004678A0: the cutscene file `path` loaded (asynchronously: `a2` passed on) with the settings {appearance,
    // profile, skater cutscene}; its music and heaps set up first. True.
    bool Load(const char* path, int a2, int unused, const uint32_t* settings);
    // 0x00467B50: whether the file has loaded (a skater cutscene's finished loading the first time it has).
    bool IsLoaded();
    // 0x00467BA0: a frame of the cutscene: the clock, skipping, the camera, subtitles, poses and transforms (and
    // the debug time / screenshots).
    void Update(void* camera, Script::CStruct* params);
    // 0x00467EA0: Update once the music is ready (the objects refreshed first).
    void Play(void* camera, Script::CStruct* params);
    // 0x00468080 (vtable slot 0): the scalar deleting destructor.
    CCutsceneData* Destroy(unsigned int flags);
    // 0x00466C20: the destructor body: the bundle, the animation sources, the objects, the lights and every asset
    // dropped; the cutscene's .qb unloaded.
    void DestroyBody();
    // 0x00466DD0: the bundle's assets taken in by type (parameters, objects, .qb, camera / object / plain
    // animations, images, models with their textures - a skater's skin only when it's the skater's); the end time.
    void LoadFromBundle();
    void LoadModel(int index, File::AssetBundle::Entry* entry, void* data);

    void** m_vtable;              // +0x00 (0x00647908)
    uint32_t m_unknown4;
    File::AssetBundle* mp_bundle; // +0x08: the cutscene's loaded bundle
    bool m_loaded;                // +0x0C
    uint8_t m_unknownD[3];
    int m_cameraParts;            // +0x10: the camera animation's parts (after the first)
    void* m_cameraSource;         // +0x14: its animation source
    int m_objectParts;            // +0x18: the object animation's parts
    void* m_objectSource;         // +0x1C
    void* mp_objectAnims;         // +0x20
    void* mp_cameraAnim;          // +0x24
    void* mp_animList;            // +0x28: the object animations' names (count at +8, 0x00482F00 the n-th)
    uint8_t m_lent2C[0x10];       // +0x2C: given back to the objects' models
    uint8_t m_lent3C;             // +0x3C
    uint8_t m_lent3D;             // +0x3D
    uint8_t m_unknown3E[2];
    float m_colour[4];            // +0x40: the colour of the first level object a cutscene object stood in for
    bool m_colourSaved;           // +0x50: (and its model's settings, +0x2C-0x3D) saved
    uint8_t m_unknown51[3];
    ObjectEntry m_objects[256];   // +0x54
    int m_objectCount;            // +0x2054
    AnimationEntry m_animations[256]; // +0x2058
    int m_animationCount;         // +0x2C58
    uint32_t m_assets[512];       // +0x2C5C
    int m_assetCount;             // +0x345C
    bool m_ready;                 // +0x3460: the music started (or none to wait for)
    bool m_skippable;             // +0x3461
    bool m_skipped;               // +0x3462
    uint8_t m_unknown3463;
    int m_cameraPart;             // +0x3464
    int m_objectPart;             // +0x3468
    float m_endTime;              // +0x346C: seconds
    float m_unknown3470;          // +0x3470 (1 at first)
    uint8_t m_unknown3474;
    uint8_t m_unknown3475[3];
    uint32_t m_unknown3478;       // +0x3478
    float m_time;                 // +0x347C: seconds into the cutscene
    float m_unknown3480;          // +0x3480
    float m_cameraStart;          // +0x3484
    uint32_t m_unknown3488;
    float m_unknown348C;          // +0x348C: where a fixed-step cutscene loops (0: never)
    uint64_t m_pausedVblanks;     // +0x3490
    uint64_t m_vblanks;           // +0x3498: counted by UpdateVblanks
    uint64_t m_lastVblanks;       // +0x34A0: the timer's count at the last UpdateVblanks
    void* m_unknown34A8;          // +0x34A8: the skater's appearance
    void* m_unknown34AC;          // +0x34AC: the skater's profile structure
    uint8_t m_flag34B0;           // +0x34B0: a skater cutscene with its own heaps, music, camera (kept as given)
    uint8_t m_unknown34B1[3];
    uint32_t m_name;              // +0x34B4: the cutscene's name checksum
    Script::CArray* m_subtitles;  // +0x34B8
    float m_subtitleDuration;     // +0x34BC: the last subtitle's (0 if it gave none)
    float m_subtitleTime;         // +0x34C0: when the last subtitle started
    float m_nextSubtitleTime;     // +0x34C4: -1: no more
    uint32_t m_nextSubtitle;      // +0x34C8: its index
    Script::CStruct* mp_params;   // +0x34CC
};

// The cutscene kind of the movie manager's movie details (vtable 0x00647920).
class CCutsceneDetails {
public:
    // 0x00463A50 (vtable slot 11): virtual slot 12 with whether +0x5D is clear.
    void CallSlot12WithNotFlag5D();
    // 0x00463A60 (vtable slot 13): while +0x60 is -1, virtual slot 12 with 0 if +0x5D is set; else with 1, then
    // +0x60 takes the global integer 0xA2A4F02F.
    void UpdateSlot12();
    // 0x00464580 (vtable slot 9): whether the loaded cutscene may be skipped (false if none is loaded).
    bool IsSkippable();
    // 0x00467F30 (vtable slot 0): the scalar deleting destructor.
    CCutsceneDetails* Destroy(unsigned int flags);
    // 0x00467F60 (vtable slot 2): once loaded: the begin script (once), then the cutscene played - or, with +0x5D,
    // only its clock and objects; +0x60 counts down to a call of slot 12.
    void Update();
    // 0x00468040 (vtable slot 5): done (the movie details' flag, or the cutscene loaded and at its end).
    bool IsDone();
    // 0x004680B0 (vtable slot 7): a skater cutscene loaded with its music started.
    bool IsReady();

    // 0x00466490 (returns this): the movie details' constructor, then the cutscene's: no data, a parameters
    // structure, the three settings (the third's low byte: whether it owns its data).
    CCutsceneDetails* Construct(const uint32_t* settings);
    // 0x00466620 (vtable slot 1): the cutscene named by 0xA1DC81F9 set up: an asset found by name, or (when it
    // owns its data) its bundle loaded with the loading screen up and the music muted (start time / loop length
    // in milliseconds, subtitles from <name>_subtitles). Whether it's there.
    bool InitFromStructure(Script::CStruct* params);
    // 0x00466A10 (vtable slot 8): the cutscene over: its objects (or its asset, heaps, the music...) dropped and its
    // end scripts run.
    void End();
    // 0x00466560: the destructor body: the parameters freed; when it owns its data, the data's asset unloaded
    // (0x00407B90 / 0x00407B20) and 0x0048E970 / 0x0048E990 given back the three values; the movie details'.
    void DestroyBody();

    void** m_vtable;
    uint8_t m_unknown4[0x10];
    void* m_camera;               // +0x14 (the movie details')
    bool m_flag18;                // +0x18
    bool m_flag19;
    bool m_done;                  // +0x1A
    bool m_flag1B;
    uint8_t m_unknown1C[0x10];
    CCutsceneData* m_data;        // +0x2C
    uint32_t m_asset;             // +0x30
    uint32_t m_beginScript;       // +0x34: the cutscene's name + "_begin"
    bool m_begun;                 // +0x38: the begin script run
    uint8_t m_unknown39[3];
    uint32_t m_endScript;         // +0x3C: + "_end"
    Script::CStruct* m_params;    // +0x40
    float m_musicVolume;          // +0x44: the volumes before the cutscene (given back at its end)
    float m_streamVolume0;        // +0x48
    float m_streamVolume1;        // +0x4C
    uint32_t m_settings[3];       // +0x50
    uint8_t m_ownsData;           // +0x5C (the third setting's low byte, as it is)
    bool m_flag5D;                // +0x5D
    uint8_t m_unknown5E[2];
    int m_unknown60;              // +0x60
};

static_assert(sizeof(CCutsceneData) == 0x34D0 && offsetof(CCutsceneData, m_unknown3470) == 0x3470 &&
                  offsetof(CCutsceneData, m_unknown348C) == 0x348C && offsetof(CCutsceneData, mp_animList) == 0x28,
              "layout must match the original");
static_assert(offsetof(CCutsceneData, m_objects) == 0x54 && offsetof(CCutsceneData, m_objectCount) == 0x2054 &&
                  offsetof(CCutsceneData, m_animationCount) == 0x2C58 && offsetof(CCutsceneData, m_assetCount) == 0x345C &&
                  offsetof(CCutsceneData, m_unknown3478) == 0x3478 && offsetof(CCutsceneData, m_flag34B0) == 0x34B0 &&
                  sizeof(CCutsceneData::ObjectEntry) == 0x20,
              "layout must match the original");
static_assert(offsetof(CCutsceneData, m_skippable) == 0x3461 && offsetof(CCutsceneData, m_endTime) == 0x346C &&
                  offsetof(CCutsceneData, m_time) == 0x347C && offsetof(CCutsceneData, m_vblanks) == 0x3498 &&
                  offsetof(CCutsceneData, m_name) == 0x34B4 && offsetof(CCutsceneData, m_nextSubtitle) == 0x34C8,
              "layout must match the original");
static_assert(offsetof(CCutsceneDetails, m_params) == 0x40 && offsetof(CCutsceneDetails, m_settings) == 0x50 &&
                  offsetof(CCutsceneDetails, m_ownsData) == 0x5C && offsetof(CCutsceneData, m_pausedVblanks) == 0x3490,
              "layout must match the original");
static_assert(offsetof(CCutsceneDetails, m_data) == 0x2C && offsetof(CCutsceneDetails, m_flag5D) == 0x5D &&
                  offsetof(CCutsceneDetails, m_unknown60) == 0x60,
              "layout must match the original");

} // namespace Obj
