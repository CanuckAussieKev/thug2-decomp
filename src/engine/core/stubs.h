#pragma once

#include <cstdint>

// Functions that do (almost) nothing, each standing for many folded ones (see stubs.cpp).
namespace stubs {

bool return_true();
bool return_false();
int return_zero();
void do_nothing();
void ignore_argument(int);
void ignore_argument(const void*); // (the same stub, given an address)
void ignore_arguments3(int, int, int);
void ignore_arguments3(const void*, int, int);
int return_zero2(int, int);
int return_zero2(const void*, int);
uint16_t return_zero_word();

} // namespace stubs
