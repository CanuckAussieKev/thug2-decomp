#pragma once

namespace Mth {

// The engine's general-purpose vector: x, y, z plus a w component. Most operations work on all four
// components; Length, Normalize, DotProduct and CrossProduct use only x, y, z.
class Vector {
public:
    float x, y, z, w;

    Vector& operator=(const Vector& other);
    Vector& operator+=(const Vector& other);
    Vector& operator*=(float scale);

    float LengthSqr() const;
    float Length() const;
    // Scales x, y, z to length 1. A zero-length vector is left unchanged.
    Vector& Normalize();
};

Vector operator+(const Vector& a, const Vector& b);
Vector operator-(const Vector& a, const Vector& b);
Vector operator*(const Vector& v, float scale);
Vector operator*(float scale, const Vector& v);
Vector operator-(const Vector& v);

float DotProduct(const Vector& a, const Vector& b);
// Cross product of the x, y, z parts; w of the result is 0.
Vector CrossProduct(const Vector& a, const Vector& b);

// A pair of points. Name not confirmed yet: only its constructor (0x00402600) has been seen so far.
struct Line {
    Vector start;
    Vector end;

    Line(const Vector& from, const Vector& to);
};

} // namespace Mth
