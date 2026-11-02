#pragma once

#include <string>
#include <string_view>

#include "engine/core/format.h"

struct ImVec4;

// NOT ORIGINAL - the level editor's printf-style ImGui texts without C varargs: the format is checked against the
// arguments' types when compiled (text::format), the text is made here and handed to ImGui's text entry points whole
// (Dear ImGui's own Text / TextDisabled / TextColored / TextWrapped / SetTooltip take a variable argument list).
//   gui::text("%s  (%d)", name, count);
//   gui::tooltip("Moves the camera to %s", where);
namespace frontend::level_editor::gui {

// ImGui::Text / TextDisabled / TextColored / TextWrapped / SetTooltip of the finished text.
void put_text(const std::string& made);
void put_disabled(const std::string& made);
void put_colored(const ImVec4& colour, const std::string& made);
void put_wrapped(const std::string& made);
void put_tooltip(const std::string& made);

template <typename... Args>
void text(text::FormatFor<Args...> format, Args... args)
{
    put_text(text::format<Args...>(format, args...));
}

template <typename... Args>
void text_disabled(text::FormatFor<Args...> format, Args... args)
{
    put_disabled(text::format<Args...>(format, args...));
}

template <typename... Args>
void text_colored(const ImVec4& colour, text::FormatFor<Args...> format, Args... args)
{
    put_colored(colour, text::format<Args...>(format, args...));
}

template <typename... Args>
void text_wrapped(text::FormatFor<Args...> format, Args... args)
{
    put_wrapped(text::format<Args...>(format, args...));
}

template <typename... Args>
void tooltip(text::FormatFor<Args...> format, Args... args)
{
    put_tooltip(text::format<Args...>(format, args...));
}

} // namespace frontend::level_editor::gui
