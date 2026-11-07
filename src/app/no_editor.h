#pragma once

namespace ext {
class Extension;
}

// NOT ORIGINAL - the editor's slot of the extension order when no plugin fills it (plugins\thug2-level-editor.dll not
// there, or not loaded - engine/ext/plugin.h): the data overlay's scripts still call the editor's script commands
// (the pause menu's LEVEL EDITOR, the level select's edited copies, an edited level's cameras), answered "no editor";
// -edit / edit_level= start the level in free skate, logging that the editor isn't installed.
namespace app {

ext::Extension& no_editor_extension();

} // namespace app
