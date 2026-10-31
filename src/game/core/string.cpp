#include "game/core/string.h"

#include <cstring>
#include <new>

#include "game/core/mem.h"

namespace TextUtil {

namespace {

int g_liveStrings;

int& LiveStrings() { return g_liveStrings; }

} // namespace

// 0x00401D80
String::String() : m_serial(LiveStrings()++), m_octets(nullptr), m_capacity(0)
{
    // The original leaves m_capacity unset; CopyRange sets it before it's ever read.
}

// 0x00401F50
String::String(const char* text) : String()
{
    CopyRange(text, 0, LastIndex(text));
}

// 0x00401FA0
String::String(const String& other) : m_serial(0), m_octets(nullptr), m_capacity(0)
{
    // Same as the original's order: copy first, then take a serial number.
    CopyRange(other.m_octets, 0, LastIndex(other.m_octets));
    m_serial = LiveStrings()++;
}

// 0x00401DA0
String::~String()
{
    if (m_octets) {
        MemSys::Free(m_octets);
        m_octets = nullptr;
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
    CopyRange(other.m_octets, 0, LastIndex(other.m_octets));
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
        if (m_octets) {
            MemSys::Free(m_octets);
            m_octets = nullptr;
        }
        return;
    }

    if (text[first] == '\0') {
        // Empty string: a fresh 1-byte allocation. (The original records a capacity of 2 here.)
        if (m_octets)
            MemSys::Free(m_octets);
        m_octets = static_cast<char*>(MemSys::New(1));
        m_octets[0] = '\0';
        m_capacity = 2;
        return;
    }

    int needed = last - first + 2;
    if (needed < 4)
        needed = 4;
    if (m_octets && m_capacity < needed) {
        MemSys::Free(m_octets);
        m_octets = nullptr;
    }
    if (!m_octets) {
        m_capacity = needed;
        m_octets = static_cast<char*>(MemSys::New(needed));
    }
    int out = 0;
    for (int i = first; i <= last; ++i)
        m_octets[out++] = text[i];
    m_octets[out] = '\0';
}

} // namespace Str

// --- replacements -------------------------------------------------------------------------------------
namespace {

using TextUtil::String;

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
