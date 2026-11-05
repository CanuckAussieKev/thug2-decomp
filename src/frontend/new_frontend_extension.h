#pragma once

namespace ext {
class Extension;
}

// NOT ORIGINAL - the new front end as an extension (engine/ext/extension.h): its input and frame, its draw over the
// game's, its device objects, the original's elements hidden before their layout, the overlay's front_end scripts.
// app/ registers it.
namespace frontend::new_frontend {

ext::Extension& extension();

} // namespace frontend::new_frontend
