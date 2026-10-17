// Self-tests of the first cutscene pieces (src/game/obj/cutscene.cpp): each original against ours on the same
// stand-in cutscene data and details, every call into game code stood in for by recording code.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/front/front_end.h"
#include "game/obj/cutscene.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/device.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CCutsceneData;
using Obj::CCutsceneDetails;
using Script::CArray;
using Script::CStruct;

alignas(16) uint8_t g_data[0x3500];             // CCutsceneData
alignas(16) uint8_t g_details[0x80];            // CCutsceneDetails
void* g_detailsVtable[16];
alignas(16) uint8_t g_structs[6][16];           // stand-in structures (their calls are stood in)
CArray g_subtitles;
uint32_t g_subtitleElements[6];
alignas(16) uint8_t g_frontEnd[sizeof(Mdl::FrontEnd)];
Mdl::FrontEndInputHandler g_handler;
alignas(16) uint8_t g_device[sizeof(Sio::Device)];
Mdl::InputControlData g_controlData;

std::vector<int> g_found;          // GetFloat: found (in turn; default not)
std::vector<float> g_floats;       // and the value
std::vector<uint32_t> g_globals;   // Script::GetInteger's answers
std::vector<uint64_t> g_vblanks;   // Tmr::GetVblanks' answers
std::vector<Saved> g_saved;
bool g_saving;

CCutsceneData* Data() { return reinterpret_cast<CCutsceneData*>(g_data); }
CCutsceneDetails* Details() { return reinterpret_cast<CCutsceneDetails*>(g_details); }

template <typename T>
T Next(std::vector<T>& answers, T otherwise)
{
    if (answers.empty())
        return otherwise;
    T value = answers.front();
    answers.erase(answers.begin());
    return value;
}
uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}
// Pointers as stable ids.
uint32_t Id(uint32_t p)
{
    auto in = [p](const void* base, size_t size) {
        auto b = reinterpret_cast<uint32_t>(base);
        return p >= b && p < b + size;
    };
    if (in(g_structs, sizeof(g_structs)))
        return 0x57000000 + (p - reinterpret_cast<uint32_t>(g_structs));
    if (in(g_data, sizeof(g_data)))
        return 0xDA000000 + (p - reinterpret_cast<uint32_t>(g_data));
    if (in(g_details, sizeof(g_details)))
        return 0xDE000000 + (p - reinterpret_cast<uint32_t>(g_details));
    if (in(&g_subtitles, sizeof(g_subtitles)))
        return 0xA7000000 + (p - reinterpret_cast<uint32_t>(&g_subtitles));
    return p;
}

void RestoreLeftTest()
{
    RemoveStandIns();
    if (g_saving)
        RestoreAll(g_saved);
    g_saving = false;
}

void InstallStandIns(StandIns& s)
{
    s.At({0x0046CC30, AddressOf(&CArray::GetElement)}, 4, [](Regs& r) {
        Record({0xCC30, Id(r.ecx), r.args[0]});
        auto* array = reinterpret_cast<CArray*>(r.ecx);
        return r.args[0] < array->m_size ? g_subtitleElements[r.args[0]] : 0xBAD0BAD0u;
    });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             auto* out = reinterpret_cast<float*>(r.args[1]);
             bool found = Next(g_found, 0) != 0;
             // (the value already there is recorded only when it lives in the data: a stack local's isn't stable)
             bool inData = Id(r.args[1]) != r.args[1];
             Record({0x66D0, Id(r.ecx), r.args[0], inData ? Id(r.args[1]) : 0x57AC0000u, r.args[2],
                     inData ? Bits(*out) : 0u});
             if (found)
                 *out = Next(g_floats, 0.0f);
             return static_cast<uint32_t>(found);
         });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(
                          &Script::RunScript))},
         0, [](Regs& r) {
             Record({0x5C41, r.args[0], Id(r.args[1]), r.args[2], r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x61, r.args[0]});
        return Next(g_globals, 0u);
    });
    s.At({0x005BE8E0, AddressOf(&Tmr::GetVblanks)}, 0, [](Regs& r) {
        Record({0xE8E0});
        uint64_t value = Next(g_vblanks, uint64_t{0});
        r.edx = static_cast<uint32_t>(value >> 32);
        return static_cast<uint32_t>(value);
    });
    s.At({0x006234FE}, 0, StandInSprintf); // the game's sprintf (not started offline)
    for (void*& entry : g_detailsVtable)
        entry = nullptr;
    g_detailsVtable[12] = s.Thunk(4, [](Regs& r) {
        Record({0x5107, Id(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
}

// The data dirty but repeatable, the stand-ins' answers cleared.
void BeginRun()
{
    g_trace.clear();
    g_found.clear();
    g_floats.clear();
    g_globals.clear();
    g_vblanks.clear();
    for (size_t i = 0; i < sizeof(g_data); ++i)
        g_data[i] = static_cast<uint8_t>(0x41 + i * 13);
    for (size_t i = 0; i < sizeof(g_details); ++i)
        g_details[i] = static_cast<uint8_t>(0x17 + i * 5);
    Details()->m_vtable = g_detailsVtable;
    std::memset(&g_subtitles, 0, sizeof(g_subtitles));
    for (int i = 0; i < 6; ++i)
        g_subtitleElements[i] = reinterpret_cast<uint32_t>(g_structs[i]);
    g_subtitles.m_size = 4;
    g_subtitles.m_elements = g_subtitleElements;
}

void RecordState()
{
    for (size_t i = 0x3460; i < 0x34D0; i += 4)
        g_trace.push_back(Id(*reinterpret_cast<uint32_t*>(g_data + i)));
    for (size_t i = 0; i < sizeof(g_details); i += 4)
        g_trace.push_back(Id(*reinterpret_cast<uint32_t*>(g_details + i)));
}

void CompareRuns(const char* what, const std::vector<Saved>& globals, const std::function<void(bool)>& steps)
{
    g_saved = globals;
    SaveAll(g_saved);
    g_saving = true;
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        InstallStandIns(*s);
        for (int run = 0; run < 2; ++run) {
            BeginRun();
            steps(run == 0);
            RecordState();
            traces[run] = g_trace;
        }
        RemoveStandIns();
    }
    RestoreAll(g_saved);
    g_saving = false;
    Compare(traces[0], traces[1], what);
}

template <typename Fn>
Fn Orig(uint32_t address) { return Original<Fn>(address); }

const char* CallNameFromPath(void* code, const char* path)
{
    const char* result;
    __asm {
        mov edx, path
        call code
        mov result, eax
    }
    return result;
}

// --- 0x004638B0 ---------------------------------------------------------------------------------------------

void TestCutsceneNameFromPath()
{
    CompareRuns("cutscene: the name from a path (folders and extension cut; no folder: the buffer as it was)",
                {{0x006F2160, 0x100, {}}}, [](bool o) {
        std::memset(&GameVariable<char>(0x006F2160), 'q', 0x100);
        const char* const paths[] = {
            "cutscenes\\cs_intro.cut", "a/b\\c/name.ext.more", "plain.txt", "dir\\", "dir/noext", "\\x.y", "",
            "nofolder", "x/.hidden", "deep\\er\\name.", "with space\\the name.cut",
        };
        for (const char* path : paths) {
            void* code = o ? OriginalFunction(0x004638B0) : reinterpret_cast<void*>(&Obj::CutsceneNameFromPathStub);
            const char* name = CallNameFromPath(code, path);
            Record({name == &GameVariable<char>(0x006F2160) ? 1u : 0u, Hash(name)});
            g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(&GameVariable<char>(0x006F2160)), 0x100));
        }
    });
}

// --- CCutsceneData ----------------------------------------------------------------------------------------------

void TestCutsceneData()
{
    std::vector<Saved> globals = {{0x007CE464, 4, {}}};
    CompareRuns("cutscene: NameChecksum, IsDone, UpdateVblanks, SetSubtitles, CheckSkip, UpdateSubtitles", globals,
                [](bool o) {
        // NameChecksum: the suffix alone, or with a number (and the newline).
        Data()->m_name = 0x1234ABCD;
        const char* const suffixes[] = {"_cam", "", "obj_with_a_longer_name"};
        for (const char* suffix : suffixes)
            for (int number : {-1, 0, 7, -2, 123456})
                Record({o ? Orig<uint32_t(__thiscall*)(CCutsceneData*, const char*, int)>(0x00463910)(Data(), suffix,
                                                                                                     number)
                          : Data()->NameChecksum(suffix, number)});
        Data()->m_name = 0;
        Record({o ? Orig<uint32_t(__thiscall*)(CCutsceneData*, const char*, int)>(0x00463910)(Data(), "x", 3)
                  : Data()->NameChecksum("x", 3)});

        // IsDone: before, at, after the end; NaNs.
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float times[][2] = {{1.0f, 2.0f}, {2.0f, 2.0f}, {3.0f, 2.0f}, {-1.0f, -2.0f}, {nan, 1.0f}, {1.0f, nan}};
        for (auto& t : times) {
            Data()->m_time = t[0];
            Data()->m_endTime = t[1];
            Record({static_cast<uint32_t>(o ? Orig<int(__thiscall*)(CCutsceneData*)>(0x004639A0)(Data())
                                            : Data()->IsDone())});
        }

        // UpdateVblanks: 64-bit sums across the low word's wrap.
        Data()->m_vblanks = 0x00000001FFFFFFF0ull;
        Data()->m_lastVblanks = 0x00000000FFFFFFFEull;
        g_vblanks = {0x0000000100000005ull, 0x0000000100000009ull, 0x0000000100000100ull, 0x00000002000000FFull,
                     0x0000000000000003ull, 0x0000000000000004ull};
        for (int i = 0; i < 3; ++i) {
            if (o)
                Orig<void(__thiscall*)(CCutsceneData*)>(0x004639C0)(Data());
            else
                Data()->UpdateVblanks();
            Record({static_cast<uint32_t>(Data()->m_vblanks), static_cast<uint32_t>(Data()->m_vblanks >> 32),
                    static_cast<uint32_t>(Data()->m_lastVblanks), static_cast<uint32_t>(Data()->m_lastVblanks >> 32)});
        }

        // Subtitles: the first one's time found or not.
        auto setSubtitles = [o](CArray* array) {
            if (o)
                Orig<void(__thiscall*)(CCutsceneData*, CArray*)>(0x00463A10)(Data(), array);
            else
                Data()->SetSubtitles(array);
            RecordState();
        };
        auto update = [o] {
            if (o)
                Orig<void(__thiscall*)(CCutsceneData*)>(0x004644A0)(Data());
            else
                Data()->UpdateSubtitles();
            RecordState();
        };
        Data()->m_nextSubtitleTime = 9.5f;
        g_found = {0};
        setSubtitles(&g_subtitles);
        g_found = {1};
        g_floats = {0.5f};
        setSubtitles(&g_subtitles);
        // Through four subtitles: before the time, at it, past it; durations found or not; the last one's next is
        // none (-1); one without a time (the local's old value).
        const float clock[] = {0.25f, 0.5f, 0.75f, 2.0f, 2.0f, 5.0f, 6.0f, 7.0f, 8.0f};
        const std::vector<int> founds[] = {{}, {1, 1}, {}, {0, 1}, {1, 1}, {1, 0}, {1}, {}, {}};
        const std::vector<float> values[] = {{}, {1.5f, 2.0f}, {}, {3.0f}, {0.25f, 5.0f}, {2.0f}, {}, {}, {}};
        for (int i = 0; i < 9; ++i) {
            Data()->m_time = clock[i];
            g_found = founds[i];
            g_floats = values[i];
            update();
        }
        // No subtitles; a negative or NaN next time; a NaN clock.
        Data()->m_subtitles = nullptr;
        Data()->m_nextSubtitleTime = 0.0f;
        update();
        Data()->m_subtitles = &g_subtitles;
        Data()->m_nextSubtitle = 0;
        for (float next : {-0.5f, nan, 0.0f}) {
            Data()->m_nextSubtitleTime = next;
            Data()->m_time = 1.0f;
            g_found = {1, 1};
            g_floats = {1.0f, 1.25f};
            update();
        }
        Data()->m_nextSubtitleTime = 0.0f;
        Data()->m_time = nan;
        update();

        // CheckSkip: the global, skippable, the time (0, NaN), the first controller (plugged in, valid), the
        // buttons.
        GameVariable<Mdl::FrontEnd*>(0x007CE464) = reinterpret_cast<Mdl::FrontEnd*>(g_frontEnd);
        std::memset(g_frontEnd, 0, sizeof(g_frontEnd));
        reinterpret_cast<Mdl::FrontEnd*>(g_frontEnd)->m_inputHandlers[0] = &g_handler;
        g_handler.m_device = g_device;
        g_handler.m_controlData = &g_controlData;
        auto* device = reinterpret_cast<Sio::Device*>(g_device);
        struct Case {
            uint32_t global;
            bool skippable;
            float time;
            bool plugged;
            uint8_t valid;
            uint32_t makes;
        };
        const Case cases[] = {
            {0, true, 1.0f, true, 1, 0x40},  {0, true, 1.0f, true, 1, 0x800}, {0, true, 1.0f, true, 1, 0x840},
            {0, true, 1.0f, true, 1, 0x3BF}, {1, true, 1.0f, true, 1, 0x40},  {0, false, 1.0f, true, 1, 0x40},
            {0, true, 0.0f, true, 1, 0x40},  {0, true, nan, true, 1, 0x40},   {0, true, -0.0f, true, 1, 0x800}, {0, true, -1.0f, true, 1, 0x800},
            {0, true, 1.0f, false, 1, 0x40}, {0, true, 1.0f, true, 0, 0x40},  {0, true, 1.0f, true, 2, 0x800},
        };
        for (const Case& c : cases) {
            Data()->m_skippable = c.skippable;
            Data()->m_skipped = false;
            Data()->m_time = c.time;
            Data()->m_endTime = 42.5f;
            g_globals = {c.global};
            std::memset(g_device, 0x6B, sizeof(g_device));
            device->m_pluggedIn = c.plugged;
            device->m_valid = c.valid;
            g_controlData.m_makes = c.makes;
            g_controlData.m_held = ~c.makes;
            if (o)
                Orig<void(__thiscall*)(CCutsceneData*)>(0x00464120)(Data());
            else
                Data()->CheckSkip();
            RecordState();
        }
    });
}

// --- CCutsceneDetails -----------------------------------------------------------------------------------------

void TestCutsceneDetails()
{
    CompareRuns("cutscene: the details' slots 9, 11 and 13", {}, [](bool o) {
        // Slot 9: skippable (none loaded: false).
        for (int loaded : {1, 0}) {
            for (int skippable : {0, 1}) {
                Details()->m_data = loaded ? Data() : nullptr;
                Data()->m_skippable = skippable != 0;
                g_data[0x3461] = static_cast<uint8_t>(skippable);
                bool r = o ? Orig<bool(__thiscall*)(CCutsceneDetails*)>(0x00464580)(Details())
                           : Details()->IsSkippable();
                Record({0x9, r});
            }
        }
        // Slots 11 and 13 with +0x5D set or clear, +0x60 -1 or not.
        for (int flag : {0, 1}) {
            for (int value : {-1, 0, 5}) {
                Details()->m_flag5D = flag != 0;
                g_details[0x5D] = static_cast<uint8_t>(flag);
                Details()->m_unknown60 = value;
                if (o)
                    Orig<void(__thiscall*)(CCutsceneDetails*)>(0x00463A50)(Details());
                else
                    Details()->CallSlot12WithNotFlag5D();
                g_globals = {0x77u + flag + static_cast<uint32_t>(value)};
                if (o)
                    Orig<void(__thiscall*)(CCutsceneDetails*)>(0x00463A60)(Details());
                else
                    Details()->UpdateSlot12();
                RecordState();
            }
        }
    });
}

} // namespace

SELFTEST(Early, TestCutsceneNameFromPath, RestoreLeftTest);
SELFTEST(Early, TestCutsceneData, RestoreLeftTest);
SELFTEST(Early, TestCutsceneDetails, RestoreLeftTest);
