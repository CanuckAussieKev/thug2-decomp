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

namespace Obj {

class CCompositeObject;

// 0x00463AA0: a matrix from a rotation (quaternion: its x, y and z negated in place - the original's) and a
// translation (w 1).
void __cdecl QuatToMatrix(float* quat, const float* translation, float* matrix);
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
        uint32_t unknown8;
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

    void** m_vtable;              // +0x00 (0x00647908)
    uint32_t m_unknown4;
    uint32_t m_unknown8;
    uint32_t m_unknownC;
    uint32_t m_unknown10;
    uint32_t m_unknown14;         // +0x14
    uint32_t m_unknown18[2];
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
    uint8_t m_unknown3460;
    bool m_skippable;             // +0x3461
    bool m_skipped;               // +0x3462
    uint8_t m_unknown3463;
    uint32_t m_unknown3464;
    uint32_t m_unknown3468;
    float m_endTime;              // +0x346C: seconds
    float m_unknown3470;          // +0x3470 (1 at first)
    uint8_t m_unknown3474;
    uint8_t m_unknown3475[3];
    uint32_t m_unknown3478;       // +0x3478
    float m_time;                 // +0x347C: seconds into the cutscene
    float m_unknown3480;          // +0x3480
    float m_cameraStart;          // +0x3484
    uint32_t m_unknown3488;
    uint32_t m_unknown348C;
    uint8_t m_unknown3490[8];
    uint64_t m_vblanks;           // +0x3498: counted by UpdateVblanks
    uint64_t m_lastVblanks;       // +0x34A0: the timer's count at the last UpdateVblanks
    void* m_unknown34A8;          // +0x34A8: the skater's appearance
    void* m_unknown34AC;          // +0x34AC: the skater's profile structure
    bool m_flag34B0;              // +0x34B0
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

    void** m_vtable;
    uint8_t m_unknown4[0x2C - 4];
    CCutsceneData* m_data;        // +0x2C
    uint8_t m_unknown30[0x5D - 0x30];
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
static_assert(offsetof(CCutsceneDetails, m_data) == 0x2C && offsetof(CCutsceneDetails, m_flag5D) == 0x5D &&
                  offsetof(CCutsceneDetails, m_unknown60) == 0x60,
              "layout must match the original");

} // namespace Obj
