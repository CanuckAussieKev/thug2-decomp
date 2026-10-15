#pragma once

namespace Mth {

// The engine's general-purpose vector: x, y, z plus a w component. Most operations work on all four
// components; Length, Normalize, DotProduct and CrossProduct use only x, y, z.
class Vector {
public:
    float x, y, z, w;

    // The original also has a Vector(x, y, z) constructor (0x004022F0) that sets w = 1. Vector stays an
    // aggregate here so it can be written as {x, y, z, w}; use {x, y, z, 1.0f} for that constructor.

    Vector& operator=(const Vector& other);
    float& operator[](int index);
    Vector& operator+=(const Vector& other);
    Vector& operator-=(const Vector& other);
    Vector& operator*=(float scale);

    // Removes the part of this vector that lies along `normal` (all four components are adjusted by
    // dot(xyz, normal.xyz) * normal). With a unit-length normal this projects onto the plane.
    Vector& ProjectToPlane(const Vector& normal);

    float LengthSqr() const;
    float Length() const;
    // Scales x, y, z to length 1. A zero-length vector is left unchanged.
    Vector& Normalize();

    // Rotate about the Y axis by 1, 2 or 3 quarter turns (other values do nothing).
    Vector& RotateY90(int quarter_turns);
    // Unit conversions applied to x, y, z (w is unchanged).
    void DegreesToRadians();
    void RadiansToDegrees();
    void FeetToInches();
    // x = -x, y = -z, z = y. Purpose not identified yet (looks like a coordinate-system conversion).
    void NegateXRotateX90();
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

    // Squared distance from `point` to the closest point on the segment (x, y, z only). If `offset` is given
    // it receives point - closest point, including w.
    float DistanceSqrToPoint(const Vector& point, Vector* offset = nullptr) const;
};

// Closest points between lines `a` and `b`: point_a = a.start + s * (a.end - a.start), point_b likewise with
// t. With `clamp`, s and t are limited to [0, 1] (segments instead of infinite lines). Returns false, leaving
// the outputs untouched, if either line is shorter than 1e-5 on every axis or they are (nearly) parallel.
bool ClosestPointsBetweenLines(const Line& a, const Line& b, Vector& point_a, Vector& point_b, float& s,
                               float& t, bool clamp);

} // namespace Mth
