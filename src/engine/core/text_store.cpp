#include "engine/core/text_store.h"

#include <cstring>
#include <new>

#include "engine/core/mem.h"
#include "engine/core/raw_view.h"

namespace text {

namespace {

int g_live_strings;

int& live_strings() { return g_live_strings; }

} // namespace

// 0x00401D80
Chars::Chars() : serial_(live_strings()++), octets_(nullptr), capacity_(0)
{
    // The original leaves capacity_ unset; copy_range sets it before it's ever read.
}

// 0x00401F50
Chars::Chars(const char* text) : Chars() { copy_range(text, 0, last_index(text)); }

// 0x00401FA0
Chars::Chars(const Chars& other) : serial_(0), octets_(nullptr), capacity_(0)
{
    // Same as the original's order: copy first, then take a serial number.
    copy_range(other.octets_, 0, last_index(other.octets_));
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    serial_ = live_strings()++;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x00401DA0
Chars::~Chars()
{
    if (octets_) {
        memory::dealloc(octets_);
        octets_ = nullptr;
    }
    --live_strings();
}

// 0x00401ED0
Chars& Chars::operator=(const char* text)
{
    copy_range(text, 0, last_index(text));
    return *this;
}

// 0x00401F10
Chars& Chars::operator=(const Chars& other)
{
    if (this == &other) // (harmless anyway: the same length needs no new buffer)
        return *this;
    copy_range(other.octets_, 0, last_index(other.octets_));
    return *this;
}

// Index of the last character (0 for null or empty text), as the original computes it inline.
int Chars::last_index(const char* text)
{
    if (!text)
        return 0;
    const int last = static_cast<int>(std::strlen(text)) - 1;
    return last < 0 ? 0 : last;
}

// 0x00401DD0
void Chars::copy_range(const char* text, int first, int last)
{
    if (!text || first > last) {
        if (octets_) {
            memory::dealloc(octets_);
            octets_ = nullptr;
        }
        return;
    }

    if (text[first] == '\0') {
        // Empty string: a fresh 1-byte allocation. (The original records a capacity of 2 here.)
        if (octets_)
            memory::dealloc(octets_);
        octets_ = static_cast<char*>(memory::alloc(1));
        octets_[0] = '\0';
        capacity_ = 2;
        return;
    }

    int needed = last - first + 2;
    if (needed < 4)
        needed = 4;
    if (octets_ && capacity_ < needed) {
        memory::dealloc(octets_);
        octets_ = nullptr;
    }
    if (!octets_) {
        capacity_ = needed;
        octets_ = static_cast<char*>(memory::alloc(needed));
    }
    int out = 0;
    for (int i = first; i <= last; ++i)
        octets_[out++] = text[i];
    octets_[out] = '\0';
}

} // namespace text
