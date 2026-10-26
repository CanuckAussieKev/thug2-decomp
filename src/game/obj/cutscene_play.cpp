// Cutscenes: loading a cutscene file and playing it (the data's Load / FinishLoading / IsLoaded / Update / Play,
// the details' remaining virtuals and both deleting destructors; 0x00467710-0x004680DE). See cutscene.h.

#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/file/asset_bundle.h"
#include "game/gfx/camera.h"
#include "game/obj/cutscene.h"
#include "game/pcm/music.h"
#include "game/pcm/music_api.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Pcm {
// (stream.h; not included here: its SetMusicVolume clashes with music.h's)
void __cdecl SyncMusicToTime(float seconds);
} // namespace Pcm

namespace Obj {

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
int& FixedFrame() { return GameVariable<int>(0x006F115C); }
char* ShotName() { return &GameVariable<char>(0x006F1058); }

void Run(uint32_t script, Script::CStruct* params = nullptr)
{
    Script::RunScript(script, params, nullptr, false, nullptr);
}
void DeleteVirtual(void* object)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(object))[0])(object, 1);
}
Script::CStruct* MakeStruct()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
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
void CCutsceneData::MarkLoadedModels()
{
    int version = 0;
    mp_params->GetInteger(kVersion, &version, 1);
    Script::CArray* objects = nullptr;
    mp_params->GetArray(kObjects, &objects, 1);
    for (uint32_t i = 0; i < objects->m_size; ++i) {
        auto* object = reinterpret_cast<Script::CStruct*>(objects->GetElement(i));
        uint32_t model;
        if (object->GetChecksum(version >= 2 ? kName : kModel, &model, 0) &&
            mp_bundle->HasAsset(model, kTypeModel))
            object->AddChecksum(kUnnamed, kKeepSkeleton);
    }
    if (m_flag34B0)
        AddObjects(mp_params);
}

// 0x004677F0
bool CCutsceneData::FinishLoading(int /*unused*/)
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(m_flag34B0 ? manager->m_cutsceneBottomUpHeap : manager->m_bottomUpHeap);
    LoadFromBundle();
    MarkLoadedModels();
    AddCutsceneLights();
    Mem::Manager::Get()->PopContext();
    if (mp_bundle)
        DeleteVirtual(mp_bundle);
    mp_bundle = nullptr;
    if (m_flag34B0 && !m_ready) {
        // the music: waiting for its stream to be ready, then started
        for (;;) {
            Pcm::Update();
            if (Pcm::PreLoadMusicStreamDone()) {
                Pcm::StartPreLoadedMusicStream(-1.0f);
                m_ready = true;
                break;
            }
            Tmr::WaitForVblank();
            if (m_ready)
                break;
        }
    }
    return true;
}

// 0x004678A0
bool CCutsceneData::Load(const char* path, int a2, int /*unused*/, const uint32_t* settings)
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
        Pcm::StopMusic();
    Stubs::ReturnTrue(); // (0x005B6570: a jump to the shared stub, true)
    const char* name = CutsceneNameFromPath(path);
    if (m_flag34B0)
        std::memcpy(ShotName(), name, std::strlen(name) + 1);
    else
        std::memcpy(ShotName(), "dummy", 6); // (the game's 0x0064799C)
    m_name = Crc::GenerateCRCFromString(name);
    if (m_flag34B0) {
        // the music: the cutscene's for the skater's model, else the cutscene's own; else none to wait for
        char stream[256];
        std::snprintf(stream, sizeof(stream), "%s_%s", name, SkaterModelName(static_cast<Script::CStruct*>(m_unknown34AC)));
        if (!Pcm::PreLoadMusicStream(Crc::GenerateCRCFromString(stream))) {
            Pcm::StopMusic();
            std::snprintf(stream, sizeof(stream), "%s", name);
            if (!Pcm::PreLoadMusicStream(Crc::GenerateCRCFromString(stream)))
                m_ready = true;
        }
        if (m_flag34B0)
            SetUpCutsceneHeaps();
    }
    GameVariable<bool>(0x006F1158) = true;
    Mem::Manager* manager = Mem::Manager::Get();
    Mem::Heap* fileHeap = m_flag34B0 ? manager->m_cutsceneBottomUpHeap : manager->m_bottomUpHeap;
    Mem::Heap* assetHeap = m_flag34B0 ? manager->m_cutsceneTopDownHeap : manager->m_topDownHeap;
    manager->PushContext(fileHeap);
    void* memory = Mem::Calloc(sizeof(File::AssetBundle));
    mp_bundle = memory ? new (memory) File::AssetBundle : nullptr;
    mp_bundle->SetHeaps(fileHeap, assetHeap);
    Mem::Manager::Get()->PopContext();
    Mem::Manager::Get()->PushContext(assetHeap);
    mp_bundle->Load(path, a2, 0);
    Mem::Manager::Get()->PopContext();
    FinishLoading(a2);
    m_loaded = true;
    return true;
}

// 0x00467B50
bool CCutsceneData::IsLoaded()
{
    if (m_loaded)
        return true;
    if (m_flag34B0) {
        if (!mp_bundle->IsLoaded())
            return false;
        FinishLoading(1);
        m_loaded = true;
        return true;
    }
    if (mp_bundle && !mp_bundle->IsLoaded())
        return false;
    return true;
}

// 0x00467BA0
void CCutsceneData::Update(void* camera, Script::CStruct* /*params*/)
{
    bool fixed = Script::GetInteger(kFixedTime) != 0;
    if (!m_flag34B0) {
        m_unknown3478 = *reinterpret_cast<uint32_t*>(&m_time);
        m_time = static_cast<float>(Tmr::GetFrameSeconds() + m_time);
    } else {
        if (!m_skippable) { // (the first update: started)
            m_skippable = true;
            Run(kStartScript);
            static_cast<Gfx::Camera*>(camera)->SetHFOV(Script::GetFloat(kFieldOfView));
            m_unknown3478 = 0;
            m_pausedVblanks = Tmr::GetVblanks();
            FixedFrame() = 0;
        }
        int rate = GameVariable<int>(0x00684FB4);
        Pcm::SyncMusicToTime(VblankSeconds(Tmr::GetVblanks() - m_pausedVblanks, rate));
        UpdateTime(reinterpret_cast<int>(camera));
        CheckSkip();
        UpdateCamera(camera);
        UpdateSubtitles();
    }
    UpdateObjectPoses();
    UpdateObjectTransforms();
    ShowHiddenObjects();
    if (!m_flag34B0)
        return;
    if (Script::GetInteger(kDebugInfo) != 0 || fixed) {
        Script::CStruct* info = MakeStruct();
        int frame = FixedFrame();
        char line[128];
        std::snprintf(line, sizeof(line), "time = %02d : %02d : %02d", frame / 30 / 60, frame / 30 % 60, frame % 30);
        info->AddString("line1", line);
        char line2[128];
        std::snprintf(line2, sizeof(line2), "time = %f sec. / %f sec.", static_cast<double>(m_time),
                      static_cast<double>(m_endTime));
        info->AddString("line2", line2);
        Script::RunScript(kInfoScript, info, nullptr, false, nullptr);
        if (info) {
            info->Clear();
            Script::DeleteStruct(info);
        }
        if (fixed && FixedFrame() > 0) {
            char shot[128];
            std::snprintf(shot, sizeof(shot), "%s%04d", ShotName(), FixedFrame() - 1);
            GameFunction<void(__cdecl*)(const char*, int)>(0x0048C100)(shot, 1); // (a screenshot)
        }
    }
    ++FixedFrame();
}

// 0x00467EA0
void CCutsceneData::Play(void* camera, Script::CStruct* params)
{
    GameVariable<bool>(0x006F1158) = true;
    if (!m_flag34B0) {
        m_ready = true;
        Update(camera, params);
        return;
    }
    RefreshObjects();
    if (!m_ready) {
        if (!Pcm::PreLoadMusicStreamDone())
            return;
        Pcm::StartPreLoadedMusicStream(-1.0f);
        m_ready = true;
    }
    Update(camera, params);
}

// 0x00468080
CCutsceneData* CCutsceneData::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00467F30
CCutsceneDetails* CCutsceneDetails::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00467F60
void CCutsceneDetails::Update()
{
    if (!m_data)
        return;
    if (!m_ownsData) {
        if (!m_begun) {
            m_data->RestartObjects();
            if (m_beginScript)
                Run(m_beginScript);
        }
        m_begun = true;
        m_data->Play(m_camera, m_params);
        return;
    }
    if (!m_data->IsLoaded())
        return;
    if (!m_begun && m_beginScript) {
        Run(m_beginScript);
        m_begun = true;
    }
    if (m_flag5D) {
        m_data->UpdateVblanks();
        m_data->RefreshObjects();
        m_data->UpdateObjectTransforms();
    } else {
        m_data->Play(m_camera, m_params);
    }
    if (m_unknown60 > -1 && --m_unknown60 == 0) {
        m_unknown60 = -1;
        reinterpret_cast<void(__thiscall*)(void*, int)>(m_vtable[0x30 / 4])(this, 1);
    }
}

// 0x00468040
bool CCutsceneDetails::IsDone()
{
    if (m_done)
        return true;
    if (m_flag18 || !m_data)
        return false;
    return m_data->IsLoaded() && m_data->IsDone();
}

// 0x004680B0
bool CCutsceneDetails::IsReady()
{
    CCutsceneData* data = m_data;
    return data && data->m_flag34B0 && data->IsLoaded() && data->m_ready;
}

} // namespace Obj

REPLACE(0x00467710, &Obj::CCutsceneData::MarkLoadedModels);
REPLACE(0x004677F0, &Obj::CCutsceneData::FinishLoading);
REPLACE(0x004678A0, &Obj::CCutsceneData::Load);
REPLACE(0x00467B50, &Obj::CCutsceneData::IsLoaded);
REPLACE(0x00467BA0, &Obj::CCutsceneData::Update);
REPLACE(0x00467EA0, &Obj::CCutsceneData::Play);
REPLACE(0x00467F30, &Obj::CCutsceneDetails::Destroy);
REPLACE(0x00467F60, &Obj::CCutsceneDetails::Update);
REPLACE(0x00468040, &Obj::CCutsceneDetails::IsDone);
REPLACE(0x00468080, &Obj::CCutsceneData::Destroy);
REPLACE(0x004680B0, &Obj::CCutsceneDetails::IsReady);
