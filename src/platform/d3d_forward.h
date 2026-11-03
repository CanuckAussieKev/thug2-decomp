#pragma once

// NOT ORIGINAL - the Direct3D 9 interfaces the engine's headers name (pointers only): <d3d9.h> itself. A forward
// declaration of a COM interface would be a second declaration of the SDK's class - one with virtual functions and
// no virtual destructor - in every file that also includes <d3d9.h>. Its <windows.h> comes with it: min / max are
// off (NOMINMAX); `near` and `far` are macros there, so the code doesn't use them as names.
#include <d3d9.h>
