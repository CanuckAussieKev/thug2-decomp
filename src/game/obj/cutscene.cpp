// Cutscenes: the first pieces of CCutsceneData and CCutsceneDetails (0x004638B0-0x004644A0). See cutscene.h.

#include "game/obj/cutscene.h"

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/front/front_end.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/device.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Obj {

GAME_GLOBAL(char[256], g_cutsceneNameBuffer, 0x006F2160);

namespace {

constexpr uint32_t kSubtitleScript = 0x625985F9;  // run with each subtitle's structure (name unknown)
constexpr uint32_t kTime = 0x906B67BA;            // a subtitle's start (name unknown)
constexpr uint32_t kDuration = 0x79A07F3F;        // and how long it shows (name unknown)
constexpr uint32_t kNoSkipping = 0xA2107A0E;      // a global integer: cutscenes can't be skipped (name unknown)
constexpr uint32_t kSlot12Global = 0xA2A4F02F;    // CCutsceneDetails::UpdateSlot12's global (name unknown)

// The skip buttons (the controller's makes).
constexpr uint32_t kButtonX = 0x40;
constexpr uint32_t kButtonStart = 0x800;

char* NameBuffer() { return g_cutsceneNameBuffer; }

} // namespace

// 0x004638B0
const char* CutsceneNameFromPath(const char* path)
{
    char* name = NameBuffer();
    for (int i = static_cast<int>(std::strlen(path)); i >= 0; --i) {
        if (path[i] == '\\' || path[i] == '/') {
            // (copied forwards a byte at a time, as the original)
            const char* from = path + i + 1;
            char* to = name;
            while ((*to++ = *from++) != '\0') {
            }
            break;
        }
    }
    if (char* extension = std::strstr(name, "."))
        *extension = '\0';
    return name;
}

// The path in EDX; the result in EAX.
__declspec(naked) void CutsceneNameFromPathStub()
{
    __asm {
        push edx
        call CutsceneNameFromPath
        add esp, 4
        ret
    }
}

// 0x00463910
uint32_t CCutsceneData::NameChecksum(const char* suffix, int number)
{
    if (number == -1)
        return Crc::ExtendCRCWithString(m_name, suffix);
    char text[0x200];
    std::snprintf(text, sizeof(text), "%s%d\n", suffix, number);
    return Crc::ExtendCRCWithString(m_name, text);
}

// 0x004639A0
int CCutsceneData::IsDone()
{
    return m_time >= m_endTime ? 1 : 0;
}

// 0x004639C0
void CCutsceneData::UpdateVblanks()
{
    m_vblanks += Tmr::GetVblanks() - m_lastVblanks;
    m_lastVblanks = Tmr::GetVblanks();
}

// 0x00463A10
void CCutsceneData::SetSubtitles(Script::CArray* subtitles)
{
    m_subtitles = subtitles;
    m_nextSubtitle = 0;
    auto* first = reinterpret_cast<Script::CStruct*>(subtitles->GetElement(0));
    first->GetFloat(kTime, &m_nextSubtitleTime, 1);
}

// 0x00464120
void CCutsceneData::CheckSkip()
{
    if (Script::GetInteger(kNoSkipping) != 0 || !m_skippable || m_time == 0.0f)
        return;
    Mdl::FrontEndInputHandler* handler = Mdl::FrontEndInstance()->m_inputHandlers[0];
    auto* device = reinterpret_cast<Sio::Device*>(handler->m_device);
    if (!device->IsPluggedIn() || !device->m_valid)
        return;
    if (handler->m_controlData->m_makes & (kButtonX | kButtonStart)) {
        m_time = m_endTime;
        m_skipped = true;
    }
}

// 0x004644A0
void CCutsceneData::UpdateSubtitles()
{
    // (comparisons as the original's: a NaN time never shows a subtitle)
    if (!m_subtitles || !(m_nextSubtitleTime >= 0.0f) || !(m_time >= m_nextSubtitleTime))
        return;
    auto* subtitle = reinterpret_cast<Script::CStruct*>(m_subtitles->GetElement(m_nextSubtitle));
    m_subtitleDuration = 0.0f;
    subtitle->GetFloat(kDuration, &m_subtitleDuration, 0);
    Script::RunScript(kSubtitleScript, subtitle, nullptr, false, nullptr);
    m_subtitleTime = m_nextSubtitleTime;
    ++m_nextSubtitle;
    m_nextSubtitleTime = -1.0f;
    if (m_nextSubtitle < m_subtitles->m_size) {
        auto* next = reinterpret_cast<Script::CStruct*>(m_subtitles->GetElement(m_nextSubtitle));
        // (a subtitle without a time leaves the local as it was: the original's holds this object's address)
        float time;
        const CCutsceneData* self = this;
        std::memcpy(&time, &self, sizeof(time));
        next->GetFloat(kTime, &time, 1);
        m_nextSubtitleTime = time;
    }
}

// 0x00463A50
void CCutsceneDetails::CallSlot12WithNotFlag5D()
{
    reinterpret_cast<void(__thiscall*)(CCutsceneDetails*, bool)>(m_vtable[12])(this, !m_flag5D);
}

// 0x00463A60
void CCutsceneDetails::UpdateSlot12()
{
    auto slot12 = reinterpret_cast<void(__thiscall*)(CCutsceneDetails*, bool)>(m_vtable[12]);
    if (m_flag5D) {
        if (m_unknown60 == -1)
            slot12(this, false);
    } else if (m_unknown60 == -1) {
        slot12(this, true);
        m_unknown60 = Script::GetInteger(kSlot12Global);
    }
}

// 0x00464580
bool CCutsceneDetails::IsSkippable()
{
    return m_data ? m_data->m_skippable : false;
}

} // namespace Obj

REPLACE_UNCHECKED(0x004638B0, Obj::CutsceneNameFromPathStub);
REPLACE(0x00463910, &Obj::CCutsceneData::NameChecksum);
REPLACE(0x004639A0, &Obj::CCutsceneData::IsDone);
REPLACE(0x004639C0, &Obj::CCutsceneData::UpdateVblanks);
REPLACE(0x00463A10, &Obj::CCutsceneData::SetSubtitles);
REPLACE(0x00463A50, &Obj::CCutsceneDetails::CallSlot12WithNotFlag5D);
REPLACE(0x00463A60, &Obj::CCutsceneDetails::UpdateSlot12);
REPLACE(0x00464120, &Obj::CCutsceneData::CheckSkip);
REPLACE(0x004644A0, &Obj::CCutsceneData::UpdateSubtitles);
REPLACE(0x00464580, &Obj::CCutsceneDetails::IsSkippable);
