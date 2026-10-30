#include "game/core/string.h"

#include <cstring>
#include <new>

#include "game/core/mem.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Str {

namespace {

GAME_GLOBAL(int, g_liveStrings, 0x00690FA0);

int& LiveStrings() { return g_liveStrings; }

} // namespace

// 0x00401D80
String::String() : m_serial(LiveStrings()++), m_buffer(nullptr), m_capacity(0)
{
    // The original leaves m_capacity unset; CopyRange sets it before it's ever read.
}

// 0x00401F50
String::String(const char* text) : String()
{
    CopyRange(text, 0, LastIndex(text));
}

// 0x00401FA0
String::String(const String& other) : m_serial(0), m_buffer(nullptr), m_capacity(0)
{
    // Same as the original's order: copy first, then take a serial number.
    CopyRange(other.m_buffer, 0, LastIndex(other.m_buffer));
    m_serial = LiveStrings()++;
}

// 0x00401DA0
String::~String()
{
    if (m_buffer) {
        Mem::Free(m_buffer);
        m_buffer = nullptr;
    }
    --LiveStrings();
}

// 0x00401ED0
String& String::operator=(const char* text)
{
    CopyRange(text, 0, LastIndex(text));
    return *this;
}

// 0x00401F10
String& String::operator=(const String& other)
{
    CopyRange(other.m_buffer, 0, LastIndex(other.m_buffer));
    return *this;
}

// Index of the last character (0 for null or empty text), as the original computes it inline.
int String::LastIndex(const char* text)
{
    if (!text)
        return 0;
    int last = static_cast<int>(std::strlen(text)) - 1;
    return last < 0 ? 0 : last;
}

// 0x00401DD0
void String::CopyRange(const char* text, int first, int last)
{
    if (!text || first > last) {
        if (m_buffer) {
            Mem::Free(m_buffer);
            m_buffer = nullptr;
        }
        return;
    }

    if (text[first] == '\0') {
        // Empty string: a fresh 1-byte allocation. (The original records a capacity of 2 here.)
        if (m_buffer)
            Mem::Free(m_buffer);
        m_buffer = static_cast<char*>(Mem::New(1));
        m_buffer[0] = '\0';
        m_capacity = 2;
        return;
    }

    int needed = last - first + 2;
    if (needed < 4)
        needed = 4;
    if (m_buffer && m_capacity < needed) {
        Mem::Free(m_buffer);
        m_buffer = nullptr;
    }
    if (!m_buffer) {
        m_capacity = needed;
        m_buffer = static_cast<char*>(Mem::New(needed));
    }
    int out = 0;
    for (int i = first; i <= last; ++i)
        m_buffer[out++] = text[i];
    m_buffer[out] = '\0';
}

} // namespace Str

// --- replacements -------------------------------------------------------------------------------------
namespace {

using Str::String;

// Constructors and destructors can't be pointed to directly; these adapters keep the original __thiscall
// shape (object in ECX, constructors return it in EAX).
struct StringAdapter {
    String* Construct() { return new (this) String(); }
    String* ConstructFromText(const char* text) { return new (this) String(text); }
    String* ConstructCopy(const String& other) { return new (this) String(other); }
    void Destroy() { reinterpret_cast<String*>(this)->~String(); }
};

// CopyRange is private; this gives the replacement table access to it.
struct StringCopyRange : String {
    void Call(const char* text, int first, int last) { CopyRange(text, first, last); }
};

} // namespace

// No self-tests: these need the game's memory manager, which doesn't exist yet when self-tests run.
REPLACE(0x00401D80, &StringAdapter::Construct);
REPLACE(0x00401DA0, &StringAdapter::Destroy);
REPLACE_SOURCE_ONLY(0x0054FFE0, &StringAdapter::Destroy); // a jump to 0x00401DA0
REPLACE(0x00401DD0, &StringCopyRange::Call);
REPLACE(0x00401ED0, static_cast<String& (String::*)(const char*)>(&String::operator=));
REPLACE(0x00401F10, static_cast<String& (String::*)(const String&)>(&String::operator=));
REPLACE(0x00401F50, &StringAdapter::ConstructFromText);
REPLACE(0x00401FA0, &StringAdapter::ConstructCopy);
