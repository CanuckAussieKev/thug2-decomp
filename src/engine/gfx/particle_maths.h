#pragma once

#include <cstdint>

#include "engine/maths/vec4.h"

// Vector helpers of the engine's particle systems (their bounding boxes, 0x00495260): a quadratic path's turning
// point and componentwise minimum / maximum. All three take registers (naked adapters).
namespace render {

// What 0x00494F10 is given on the stack (by value, caller-popped): the path's start, two more points and the two
// times their offsets from the start are scaled by.
struct QuadraticPoints {
    maths::Vec4 start; // +0x00
    maths::Vec4 p1;    // +0x10 (its offset scaled by t2)
    maths::Vec4 p2;    // +0x20 (its offset scaled by t1)
    float t1;          // +0x30
    float t2;          // +0x34
};

// 0x00494F10 (out in ECX, SQuadraticPoints on the stack; returns out): per component, the quadratic's coefficients
// (from the offsets and 1 / ((t2 - t1) t1 t2)) and its value at the turning point when the t^2 coefficient is
// positive, else the start; w 0. The original's x87 order and roundings (each store to a float local) kept.
maths::Vec4* __cdecl quadratic_lowest(maths::Vec4* out, const QuadraticPoints* points);
void quadratic_lowest_stub();
// 0x00495130 / 0x004951B0 (a in ECX, b in EDX, out in ESI; returns out): the smaller / larger of each component
// (b's only when strictly so; w through the x87, so a signalling NaN comes out quiet).
maths::Vec4* __cdecl min_vector(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b);
maths::Vec4* __cdecl max_vector(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b);
void min_vector_stub();
void max_vector_stub();

} // namespace Nx
