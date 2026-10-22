#pragma once

#include <cstring>

// The C string calls the online code (profiles, buddies, content vault, stats) makes on GameSpy's answers, with the
// original's semantics: unbounded copies, and strtok's one hidden position shared by every caller.
namespace GameNet::Text {

// strcpy: no length limit, as the original's.
inline char* Copy(char* to, const char* from)
{
    std::memcpy(to, from, std::strlen(from) + 1);
    return to;
}

// strtok: the next token of `text` (null: of the text given last) split at any of `delimiters`.
inline char* Token(char* text, const char* delimiters)
{
    static char* s_position = nullptr;
    return strtok_s(text, delimiters, &s_position);
}

} // namespace GameNet::Text
