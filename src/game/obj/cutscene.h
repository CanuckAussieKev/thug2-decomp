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
}

namespace Obj {

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

    uint8_t m_unknown0[0x3461];
    bool m_skippable;             // +0x3461
    bool m_skipped;               // +0x3462
    uint8_t m_unknown3463[0x346C - 0x3463];
    float m_endTime;              // +0x346C: seconds
    uint8_t m_unknown3470[0x347C - 0x3470];
    float m_time;                 // +0x347C: seconds into the cutscene
    uint8_t m_unknown3480[0x3498 - 0x3480];
    uint64_t m_vblanks;           // +0x3498: counted by UpdateVblanks
    uint64_t m_lastVblanks;       // +0x34A0: the timer's count at the last UpdateVblanks
    uint8_t m_unknown34A8[0x34B4 - 0x34A8];
    uint32_t m_name;              // +0x34B4: the cutscene's name checksum
    Script::CArray* m_subtitles;  // +0x34B8
    float m_subtitleDuration;     // +0x34BC: the last subtitle's (0 if it gave none)
    float m_subtitleTime;         // +0x34C0: when the last subtitle started
    float m_nextSubtitleTime;     // +0x34C4: -1: no more
    uint32_t m_nextSubtitle;      // +0x34C8: its index
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

static_assert(offsetof(CCutsceneData, m_skippable) == 0x3461 && offsetof(CCutsceneData, m_endTime) == 0x346C &&
                  offsetof(CCutsceneData, m_time) == 0x347C && offsetof(CCutsceneData, m_vblanks) == 0x3498 &&
                  offsetof(CCutsceneData, m_name) == 0x34B4 && offsetof(CCutsceneData, m_nextSubtitle) == 0x34C8,
              "layout must match the original");
static_assert(offsetof(CCutsceneDetails, m_data) == 0x2C && offsetof(CCutsceneDetails, m_flag5D) == 0x5D &&
                  offsetof(CCutsceneDetails, m_unknown60) == 0x60,
              "layout must match the original");

} // namespace Obj
