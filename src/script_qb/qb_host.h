#pragma once

#include "engine/script/host.h"

// NOT ORIGINAL - the QB interpreter as the game's script runtime (script::Host, engine/script/host.h): each method is
// the interpreter's function of that name (qb_script.h), called as the game called it. app/ installs it at start-up
// (app/main.cpp), and only app/ includes this header (tools/script_headers.test.ts).
namespace script::qb {

// The interpreter's runtime (made on first use, never destroyed).
Host& host();

} // namespace script::qb
