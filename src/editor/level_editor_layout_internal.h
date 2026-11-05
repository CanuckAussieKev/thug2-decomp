#pragma once

// NOT ORIGINAL - what level_editor_layout.cpp's pieces share (the text helpers and records of a saved layout). They
// were the one file's anonymous namespace; here they are in `internal`, with a using-directive, so every piece sees
// them as it saw them there.

#include <windows.h>
#include <gsl/pointers>
