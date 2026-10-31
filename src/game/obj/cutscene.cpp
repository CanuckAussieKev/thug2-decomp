// Cutscenes: the first pieces of CCinematicTable and CCinematicEntry (0x004638B0-0x004644A0). See cutscene.h.

#include "game/obj/cutscene.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/checksums.h"
#include "game/front/front_end.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/pad_input/device.h"
#include "game/sys/stopwatch.h"

namespace GameObjects {

char g_cutsceneNameBuffer[256];

namespace {

constexpr uint32_t kSubtitleScript = 0x625985F9;  // run with each subtitle's structure (name unknown)
constexpr uint32_t kTime = 0x906B67BA;            // a subtitle's start (name unknown)
constexpr uint32_t kDuration = 0x79A07F3F;        // and how long it shows (name unknown)
constexpr uint32_t kNoSkipping = 0xA2107A0E;      // a global integer: cutscenes can't be skipped (name unknown)
constexpr uint32_t kSlot12Global = 0xA2A4F02F;    // CCinematicEntry::UpdateSlot12's global (name unknown)

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
uint32_t CCinematicTable::HashOfName(const char* suffix, int number)
{
    if (number == -1)
        return Checksums::FoldTextIntoChecksum(m_label, suffix);
    char text[0x200];
    std::snprintf(text, sizeof(text), "%s%d\n", suffix, number);
    return Checksums::FoldTextIntoChecksum(m_label, text);
}

// 0x004639A0
int CCinematicTable::IsFinished()
{
    return m_clock >= m_endTime ? 1 : 0;
}

// 0x004639C0
void CCinematicTable::UpdateVblanks()
{
    m_vblanks += FrameClock::FetchRetraceCount() - m_prevRetraces;
    m_prevRetraces = FrameClock::FetchRetraceCount();
}

// 0x00463A10
void CCinematicTable::SetSubtitles(Scripts::CArray* subtitles)
{
    m_subtitles = subtitles;
    m_nextSubtitle = 0;
    auto* first = reinterpret_cast<Scripts::CStruct*>(subtitles->FetchPart(0));
    first->FetchFloat(kTime, &m_nextSubtitleTime, 1);
}

// 0x00464120
void CCinematicTable::CheckSkip()
{
    if (Scripts::FetchIntValue(kNoSkipping) != 0 || !m_canSkip || m_clock == 0.0f)
        return;
    GameModules::FrontEndInputHandler* handler = GameModules::FrontEndInstance()->m_padListeners[0];
    auto* device = reinterpret_cast<Sio::InputPort*>(handler->m_dev);
    if (!device->IsAttached() || !device->m_isUsable)
        return;
    if (handler->m_controlData->m_makes & (kButtonX | kButtonStart)) {
        m_clock = m_endTime;
        m_skipped = true;
    }
}

// 0x004644A0
void CCinematicTable::UpdateSubtitles()
{
    // (comparisons as the original's: a NaN time never shows a subtitle)
    if (!m_subtitles || !(m_nextSubtitleTime >= 0.0f) || !(m_clock >= m_nextSubtitleTime))
        return;
    auto* subtitle = reinterpret_cast<Scripts::CStruct*>(m_subtitles->FetchPart(m_nextSubtitle));
    m_subtitleDuration = 0.0f;
    subtitle->FetchFloat(kDuration, &m_subtitleDuration, 0);
    Scripts::LaunchScript(kSubtitleScript, subtitle, nullptr, false, nullptr);
    m_subtitleTime = m_nextSubtitleTime;
    ++m_nextSubtitle;
    m_nextSubtitleTime = -1.0f;
    if (m_nextSubtitle < m_subtitles->m_extent) {
        auto* next = reinterpret_cast<Scripts::CStruct*>(m_subtitles->FetchPart(m_nextSubtitle));
        // (a subtitle without a time leaves the local as it was: the original's holds this object's address)
        float time;
        const CCinematicTable* self = this;
        std::memcpy(&time, &self, sizeof(time));
        next->FetchFloat(kTime, &time, 1);
        m_nextSubtitleTime = time;
    }
}

// 0x00463A50
void CCinematicEntry::CallSlot12WithNotFlag5D()
{
    reinterpret_cast<void(__thiscall*)(CCinematicEntry*, bool)>(m_vtable[12])(this, !m_flag5D);
}

// 0x00463A60
void CCinematicEntry::UpdateSlot12()
{
    auto slot12 = reinterpret_cast<void(__thiscall*)(CCinematicEntry*, bool)>(m_vtable[12]);
    if (m_flag5D) {
        if (m_unknown60 == -1)
            slot12(this, false);
    } else if (m_unknown60 == -1) {
        slot12(this, true);
        m_unknown60 = Scripts::FetchIntValue(kSlot12Global);
    }
}

// 0x00464580
bool CCinematicEntry::CanBeSkipped()
{
    return m_content ? m_content->m_canSkip : false;
}

} // namespace Obj
