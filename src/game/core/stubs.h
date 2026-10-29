#pragma once

#include <cstdint>

// Functions that do (almost) nothing, each standing for many folded ones (see stubs.cpp).
namespace Stubs {

bool ReturnTrue();
bool ReturnFalse();
int ReturnZero();
void DoNothing();
void __stdcall IgnoreArgument(int);
void __stdcall IgnoreArguments2(int, int);
void __stdcall IgnoreArguments3(int, int, int);
bool __stdcall ReturnTrue2(int, int);
// Methods ignoring their stack arguments (the count in the name) - the engine's empty virtual slots.
bool __stdcall ReturnFalse1(int);
bool __stdcall ReturnFalse2(int, int);
bool __stdcall ReturnFalse4(int, int, int, int);
bool __stdcall ReturnFalse5(int, int, int, int, int);
bool __stdcall ReturnFalse7(int, int, int, int, int, int, int);
int __stdcall ReturnZero1(int);
int __stdcall ReturnZero2(int, int);
int __stdcall ReturnZero3(int, int, int);
int __stdcall ReturnZero6(int, int, int, int, int, int);
uint16_t ReturnZeroWord();

} // namespace Stubs
