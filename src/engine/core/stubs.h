#pragma once

#include <cstdint>

// Functions that do (almost) nothing, each standing for many folded ones (see stubs.cpp).
namespace stubs {

bool return_true();
bool return_false();
int return_zero();
void do_nothing();
void __stdcall ignore_argument(int);
void __stdcall ignore_arguments2(int, int);
void __stdcall ignore_arguments3(int, int, int);
bool __stdcall return_true2(int, int);
// Methods ignoring their stack arguments (the count in the name) - the engine's empty virtual slots.
bool __stdcall return_false1(int);
bool __stdcall return_false2(int, int);
bool __stdcall return_false4(int, int, int, int);
bool __stdcall return_false5(int, int, int, int, int);
bool __stdcall return_false7(int, int, int, int, int, int, int);
int __stdcall return_zero1(int);
int __stdcall return_zero2(int, int);
int __stdcall return_zero3(int, int, int);
int __stdcall return_zero6(int, int, int, int, int, int);
uint16_t return_zero_word();

} // namespace stubs
