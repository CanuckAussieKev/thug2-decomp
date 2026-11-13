#pragma once

// NOT ORIGINAL - the settings in effect, for the screenshot sidecar (devtools/screenshot.h): a [settings] section that
// says what a picture was taken with, so it can be reproduced. The [Graphics] keys with their value (the ini's, or the
// default marked "(default)"), the [features] keys, the [debug] keys that are on, and what the game actually runs when
// that differs from the ini (the renderer, ray tracing, the upscaler, the sun, the texture set - and the keys changed
// since the start that wait for a restart). Nothing from [Miscellaneous] (the online identity).
namespace ext {
class Extension;
}

namespace app {

// The extension whose describe() makes the section (registered by app/extensions.cpp).
ext::Extension& settings_report_extension();

} // namespace app
