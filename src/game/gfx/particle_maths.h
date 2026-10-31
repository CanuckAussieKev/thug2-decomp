#pragma once

#include <cstdint>

#include "game/mth/vector.h"

// Vector helpers of the engine's particle systems (their bounding boxes, 0x00495260): a quadratic path's turning
// point and componentwise minimum / maximum. All three take registers (naked adapters).
namespace Engine3D {

// What 0x00494F10 is given on the stack (by value, caller-popped): the path's start, two more points and the two
// times their offsets from the start are scaled by.
struct sQuadraticPoints {
    Maths::Vector start; // +0x00
    Maths::Vector p1;    // +0x10 (its offset scaled by t2)
    Maths::Vector p2;    // +0x20 (its offset scaled by t1)
    float t1;          // +0x30
    float t2;          // +0x34
};

// 0x00494F10 (out in ECX, sQuadraticPoints on the stack; returns out): per component, the quadratic's coefficients
// (from the offsets and 1 / ((t2 - t1) t1 t2)) and its value at the turning point when the t^2 coefficient is
// positive, else the start; w 0. The original's x87 order and roundings (each store to a float local) kept.
Maths::Vector* __cdecl QuadraticLowest(Maths::Vector* out, const sQuadraticPoints* points);
void QuadraticLowestStub();
// 0x00495130 / 0x004951B0 (a in ECX, b in EDX, out in ESI; returns out): the smaller / larger of each component
// (b's only when strictly so; w through the x87, so a signalling NaN comes out quiet).
Maths::Vector* __cdecl MinVector(Maths::Vector* out, const Maths::Vector* a, const Maths::Vector* b);
Maths::Vector* __cdecl MaxVector(Maths::Vector* out, const Maths::Vector* a, const Maths::Vector* b);
void MinVectorStub();
void MaxVectorStub();

} // namespace Nx
