#pragma once

namespace ext {
class Extension;
}

// NOT ORIGINAL - the dev tools as extensions (engine/ext/extension.h), one each. app/ registers them.
namespace frontend::begin_level {
// -level / -edit (start_level.h): the intro movies skipped for -edit.
ext::Extension& extension();
} // namespace frontend::begin_level
namespace frontend::automation {
// The test-automation channel (automation.h): the buttons a `press` holds.
ext::Extension& extension();
} // namespace frontend::automation
namespace frontend::test_view {
// The camera lock (test_view.h): viewport 0's camera held at a pose; TeleportSkaterStanding.
ext::Extension& extension();
} // namespace frontend::test_view
namespace sys::screenshot {
// The screenshot key (screenshot.h): its key, the frame read before it's presented, the camera noted, a frame series,
// and the pictures and notices our debugging aids ask for.
ext::Extension& extension();
} // namespace sys::screenshot
