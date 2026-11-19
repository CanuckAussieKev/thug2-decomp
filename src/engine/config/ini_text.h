#pragma once

// NOT ORIGINAL - thug2-decomp.ini as text, the way GetPrivateProfileString reads it: [section] lines, key=value lines,
// ';' / '#' comment lines; names in any case, spaces round them ignored; only the first section of a name counts (a
// second [Graphics] is ignored, as Windows does). Read and changed in place - pure (no files): the settings model's
// values (settings_model.h IniValues) and the Options screen's writes.

#include <string>

#include "engine/config/settings_model.h"

namespace config {

// A setting's place in an ini file: its [section] and its key.
struct IniKey {
    std::string section;
    std::string key;
};

// The value of `at`'s key in its section (the first such line of the first such section). False if there is none.
bool ini_get(const std::string& text, const IniKey& at, std::string* value);
// The text with `at`'s key = `value` in its section: the first such line's value replaced (its spacing before the '='
// kept), or a line added after the section's last line that isn't blank (a missing section added at the end) - every
// other line, comment and unknown key as it was, the file's line ending kept (\r\n unless it only uses \n).
std::string ini_set(const std::string& text, const IniKey& at, const std::string& value);
// The text without `at`'s key line (the first of its section; the setting back at its default) - every other line,
// comment and line ending as it was; the text as it was when there is none.
std::string ini_remove(const std::string& text, const IniKey& at);
// Every key=value of the text (the first of each, in the first section of each name), for the resolver.
IniValues ini_values(const std::string& text);

} // namespace config
