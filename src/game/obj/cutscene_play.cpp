// Cutscenes: loading a cutscene file and playing it (the data's Load / FinishLoading / IsLoaded / Update / Play,
// the details' remaining virtuals and both deleting destructors; 0x00467710-0x004680DE). See cutscene.h.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/checksums.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/file/asset_bundle.h"
#include "game/gfx/view_cam.h"
#include "game/gfx/engine_glue.h"
#include "game/obj/cutscene.h"
#include "game/audio/music_player.h"
#include "game/audio/music_api.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/sys/game_config.h"

namespace AudioStream {
// (stream.h; not included here: its SetMusicVolume clashes with music_player.h's)
void __cdecl SyncMusicToTime(float seconds);
} // namespace Pcm

namespace GameObjects {

char g_cutsceneShotName[256];
bool g_cutscenePlaying;
int g_cutsceneFixedFrame;

namespace {

constexpr uint32_t kUnnamed = 0;
constexpr uint32_t kVersion = 0x40E32C3C;
constexpr uint32_t kObjects = 0x4DE5330C;
constexpr uint32_t kName = 0x2BFC2486;
constexpr uint32_t kModel = 0xB974F2FC;
constexpr uint32_t kKeepSkeleton = 0x0CBEC8AC;
constexpr uint32_t kTypeModel = 0xFD8697E1;
constexpr uint32_t kFixedTime = 0x0C00425A;    // a global integer: the time from the frame counter
constexpr uint32_t kDebugInfo = 0x5ECC0073;    // a global integer: show the time
constexpr uint32_t kStartScript = 0xE794A164;
constexpr uint32_t kFieldOfView = 0x99529205;  // a global float: the cutscene camera's
constexpr uint32_t kInfoScript = 0xD9157174;

// The frame counter while the time is fixed (one frame each update), and the screenshots' file name.
int& FixedFrame() { return g_cutsceneFixedFrame; }
char* ShotName() { return g_cutsceneShotName; }

void Run(uint32_t script, Scripts::CStruct* params = nullptr)
{
    Scripts::LaunchScript(script, params, nullptr, false, nullptr);
}
void DeleteVirtual(void* object)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(object))[0])(object, 1);
}
Scripts::CStruct* MakeStruct()
{
    Scripts::CStruct* memory = Scripts::SpawnParamBag();
    return memory ? memory->PrepareAll() : nullptr;
}

// The vblank count since a time, in seconds (the x87's unsigned 64-bit conversion).
float VblankSeconds(uint64_t vblanks, int rate)
{
    uint64_t lowPart[1] = {vblanks & 0x7FFFFFFFFFFFFFFFull};
    uint64_t highPart[1] = {vblanks & 0x8000000000000000ull};
    float result;
    __asm {
        fild qword ptr [lowPart]
        fild qword ptr [highPart]
        fchs
        faddp st(1), st
        fidiv dword ptr [rate]
        fstp dword ptr [result]
    }
    return result;
}

} // namespace

// 0x00467710
void CCinematicTable::MarkLoadedModels()
{
    int version = 0;
    m_callArgs->FetchIntValue(kVersion, &version, 1);
    Scripts::CArray* objects = nullptr;
    m_callArgs->FetchList(kObjects, &objects, 1);
    for (uint32_t i = 0; i < objects->m_extent; ++i) {
        auto* object = reinterpret_cast<Scripts::CStruct*>(objects->FetchPart(i));
        uint32_t model;
        if (object->ReadHashValue(version >= 2 ? kName : kModel, &model, 0) &&
            m_bundlePtr->HasAsset(model, kTypeModel))
            object->PutNameKey(kUnnamed, kKeepSkeleton);
    }
    if (m_flag34B0)
        AddObjects(m_callArgs);
}

// 0x004677F0
bool CCinematicTable::FinishLoading(int /*unused*/)
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(m_flag34B0 ? manager->m_cinematicLowHeap : manager->m_bottomUpHeap);
    LoadFromBundle();
    MarkLoadedModels();
    AddCutsceneLights();
    MemSys::Hub::Pull()->LeaveHeapScope();
    if (m_bundlePtr)
        DeleteVirtual(m_bundlePtr);
    m_bundlePtr = nullptr;
    if (m_flag34B0 && !m_ready) {
        // the music: waiting for its stream to be ready, then started
        for (;;) {
            AudioStream::TickFrame();
            if (AudioStream::IsMusicTrackPrimed()) {
                AudioStream::BeginPrimedMusicTrack(-1.0f);
                m_ready = true;
                break;
            }
            FrameClock::WaitForVblank();
            if (m_ready)
                break;
        }
    }
    return true;
}

// 0x004678A0
bool CCinematicTable::ReadIn(const char* path, int a2, int /*unused*/, const uint32_t* settings)
{
    if (settings) {
        m_unknown34A8 = reinterpret_cast<void*>(settings[0]);
        m_unknown34AC = reinterpret_cast<void*>(settings[1]);
        m_flag34B0 = static_cast<uint8_t>(settings[2]);
    } else {
        m_unknown34A8 = nullptr;
        m_unknown34AC = nullptr;
        m_flag34B0 = 0;
    }
    if (m_flag34B0)
        AudioStream::StopMusic();
    Stubs::ReturnTrue(); // (0x005B6570: a jump to the shared stub, true)
    const char* name = CutsceneNameFromPath(path);
    if (m_flag34B0)
        std::memcpy(ShotName(), name, std::strlen(name) + 1);
    else
        std::memcpy(ShotName(), "dummy", 6); // (the game's 0x0064799C)
    m_label = Checksums::HashFromText(name);
    if (m_flag34B0) {
        // the music: the cutscene's for the skater's model, else the cutscene's own; else none to wait for
        char stream[256];
        std::snprintf(stream, sizeof(stream), "%s_%s", name, SkaterModelName(static_cast<Scripts::CStruct*>(m_unknown34AC)));
        if (!AudioStream::PrimeMusicTrack(Checksums::HashFromText(stream))) {
            AudioStream::StopMusic();
            std::snprintf(stream, sizeof(stream), "%s", name);
            if (!AudioStream::PrimeMusicTrack(Checksums::HashFromText(stream)))
                m_ready = true;
        }
        if (m_flag34B0)
            SetUpCutsceneHeaps();
    }
    g_cutscenePlaying = true;
    MemSys::Hub* manager = MemSys::Hub::Pull();
    MemSys::Arena* fileHeap = m_flag34B0 ? manager->m_cinematicLowHeap : manager->m_bottomUpHeap;
    MemSys::Arena* assetHeap = m_flag34B0 ? manager->m_cinematicHighHeap : manager->m_topDownHeap;
    manager->EnterHeapScope(fileHeap);
    void* memory = MemSys::AllocZeroed(sizeof(FileLoad::AssetBundle));
    m_bundlePtr = memory ? new (memory) FileLoad::AssetBundle : nullptr;
    m_bundlePtr->SetHeaps(fileHeap, assetHeap);
    MemSys::Hub::Pull()->LeaveHeapScope();
    MemSys::Hub::Pull()->EnterHeapScope(assetHeap);
    m_bundlePtr->ReadIn(path, a2, 0);
    MemSys::Hub::Pull()->LeaveHeapScope();
    FinishLoading(a2);
    m_readyFlag = true;
    return true;
}

// 0x00467B50
bool CCinematicTable::IsLoaded()
{
    if (m_readyFlag)
        return true;
    if (m_flag34B0) {
        if (!m_bundlePtr->IsLoaded())
            return false;
        FinishLoading(1);
        m_readyFlag = true;
        return true;
    }
    if (m_bundlePtr && !m_bundlePtr->IsLoaded())
        return false;
    return true;
}

// 0x00467BA0
void CCinematicTable::TickFrame(void* camera, Scripts::CStruct* /*params*/)
{
    bool fixed = Scripts::FetchIntValue(kFixedTime) != 0;
    if (!m_flag34B0) {
        m_unknown3478 = *reinterpret_cast<uint32_t*>(&m_clock);
        m_clock = static_cast<float>(FrameClock::GetFrameSeconds() + m_clock);
    } else {
        if (!m_canSkip) { // (the first update: started)
            m_canSkip = true;
            Run(kStartScript);
            static_cast<Gfx2D::ViewCam*>(camera)->ChangeHFov(Scripts::FetchFloat(kFieldOfView));
            m_unknown3478 = 0;
            m_pausedVblanks = FrameClock::FetchRetraceCount();
            FixedFrame() = 0;
        }
        int rate = GameConfig::g_frameRate;
        AudioStream::SyncMusicToTime(VblankSeconds(FrameClock::FetchRetraceCount() - m_pausedVblanks, rate));
        UpdateTime(reinterpret_cast<int>(camera));
        CheckSkip();
        RefreshViewer(camera);
        UpdateSubtitles();
    }
    UpdateObjectPoses();
    UpdateObjectTransforms();
    ShowHiddenObjects();
    if (!m_flag34B0)
        return;
    if (Scripts::FetchIntValue(kDebugInfo) != 0 || fixed) {
        Scripts::CStruct* info = MakeStruct();
        int frame = FixedFrame();
        char line[128];
        std::snprintf(line, sizeof(line), "time = %02d : %02d : %02d", frame / 30 / 60, frame / 30 % 60, frame % 30);
        info->AppendWord("line1", line);
        char line2[128];
        std::snprintf(line2, sizeof(line2), "time = %f sec. / %f sec.", static_cast<double>(m_clock),
                      static_cast<double>(m_endTime));
        info->AppendWord("line2", line2);
        Scripts::LaunchScript(kInfoScript, info, nullptr, false, nullptr);
        if (info) {
            info->Clear();
            Scripts::DeleteStruct(info);
        }
        if (fixed && FixedFrame() > 0) {
            char shot[128];
            std::snprintf(shot, sizeof(shot), "%s%04d", ShotName(), FixedFrame() - 1);
            Engine3D::ScreenShot(shot, 1); // (a screenshot)
        }
    }
    ++FixedFrame();
}

// 0x00467EA0
void CCinematicTable::Perform(void* camera, Scripts::CStruct* params)
{
    g_cutscenePlaying = true;
    if (!m_flag34B0) {
        m_ready = true;
        TickFrame(camera, params);
        return;
    }
    RefreshObjects();
    if (!m_ready) {
        if (!AudioStream::IsMusicTrackPrimed())
            return;
        AudioStream::BeginPrimedMusicTrack(-1.0f);
        m_ready = true;
    }
    TickFrame(camera, params);
}

// 0x00468080
CCinematicTable* CCinematicTable::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00467F30
CCinematicEntry* CCinematicEntry::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00467F60
void CCinematicEntry::TickFrame()
{
    if (!m_content)
        return;
    if (!m_ownsData) {
        if (!m_begun) {
            m_content->RestartObjects();
            if (m_beginScript)
                Run(m_beginScript);
        }
        m_begun = true;
        m_content->Perform(m_viewer, m_args);
        return;
    }
    if (!m_content->IsLoaded())
        return;
    if (!m_begun && m_beginScript) {
        Run(m_beginScript);
        m_begun = true;
    }
    if (m_flag5D) {
        m_content->UpdateVblanks();
        m_content->RefreshObjects();
        m_content->UpdateObjectTransforms();
    } else {
        m_content->Perform(m_viewer, m_args);
    }
    if (m_unknown60 > -1 && --m_unknown60 == 0) {
        m_unknown60 = -1;
        reinterpret_cast<void(__thiscall*)(void*, int)>(m_vtable[0x30 / 4])(this, 1);
    }
}

// 0x00468040
bool CCinematicEntry::IsFinished()
{
    if (m_isFinished)
        return true;
    if (m_flag18 || !m_content)
        return false;
    return m_content->IsLoaded() && m_content->IsFinished();
}

// 0x004680B0
bool CCinematicEntry::IsReady()
{
    CCinematicTable* data = m_content;
    return data && data->m_flag34B0 && data->IsLoaded() && data->m_ready;
}

} // namespace Obj
