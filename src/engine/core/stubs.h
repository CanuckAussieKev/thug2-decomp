#pragma once

#include <cstdint>

// Functions that do (almost) nothing, each standing for many folded ones (see stubs.cpp).
namespace stubs {

bool return_true();
bool return_false();
int return_zero();
void do_nothing();
void ignore_argument(int /*unused*/);
void ignore_argument(const void* /*unused*/); // (the same stub, given an address)
void ignore_arguments3(int /*unused*/, int /*unused*/, int /*unused*/);
void ignore_arguments3(const void* /*unused*/, int /*unused*/, int /*unused*/);
int return_zero2(int /*unused*/, int /*unused*/);
int return_zero2(const void* /*unused*/, int /*unused*/);
uint16_t return_zero_word();

} // namespace stubs
