#pragma once

namespace Str {

// The engine's owning string. Memory comes from the game's allocator.
class String {
public:
    String();
    String(const char* text);
    String(const String& other);
    ~String();

    String& operator=(const char* text);
    String& operator=(const String& other);

    const char* c_str() const { return m_buffer; }

    // Number of live String objects when this one was created; the destructor decrements the same global
    // counter. Probably a leak-tracking aid.
    int m_serial;
    char* m_buffer;
    int m_capacity;

protected:
    // Replace the contents with text[first..last] (inclusive). A null `text` or first > last frees the buffer
    // (leaving a null string); text[first] == 0 gives an empty (non-null) string.
    void CopyRange(const char* text, int first, int last);
    static int LastIndex(const char* text);
};

} // namespace Str
