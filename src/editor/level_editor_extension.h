#pragma once

namespace ext {
class Extension;
}

// NOT ORIGINAL - the level editor as an extension (engine/ext/extension.h): the points where the engine and the game
// call it - its frame, its camera in the viewports, the keyboard while it runs, its saved layouts once a level is
// up, the career save, the screenshot's sidecar. app/ registers it.
namespace frontend::level_editor {

ext::Extension& extension();

} // namespace frontend::level_editor
