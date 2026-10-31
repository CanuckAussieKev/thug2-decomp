#pragma once

namespace text {

// The engine's owning string. Memory comes from the game's allocator.
class Chars {
public:
    Chars();
    Chars(const char* text);
    Chars(const Chars& other);
    ~Chars();

    Chars& operator=(const char* text);
    Chars& operator=(const Chars& other);

    const char* c_str() const { return octets_; }

    // Number of live String objects when this one was created; the destructor decrements the same global
    // counter. Probably a leak-tracking aid.
    int serial_;
    char* octets_;
    int capacity_;

protected:
    // Replace the contents with text[first..last] (inclusive). A null `text` or first > last frees the buffer
    // (leaving a null string); text[first] == 0 gives an empty (non-null) string.
    void copy_range(const char* text, int first, int last);
    static int last_index(const char* text);
};

} // namespace text
