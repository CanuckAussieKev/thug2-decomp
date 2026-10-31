#pragma once

namespace Maths {

class Mat4;

// The engine's general-purpose vector: x, y, z plus a w component. Most operations work on all four
// components; Length, Normalize, DotVec and CrossVec use only x, y, z.
class Vec4 {
public:
    float x, y, z, w;

    // The original also has a Vec4(x, y, z) constructor (0x004022F0) that sets w = 1. Vector stays an
    // aggregate here so it can be written as {x, y, z, w}; use {x, y, z, 1.0f} for that constructor.

    Vec4& operator=(const Vec4& other);
    float& operator[](int index);
    Vec4& operator+=(const Vec4& other);
    Vec4& operator-=(const Vec4& other);
    Vec4& operator*=(float scale);
    // 0x00423650: all four components times 1 / divisor (the reciprocal unrounded).
    Vec4& operator/=(float divisor);
    // 0x004FC370 (written out with the skater's core physics): all four components 0 when shorter than `length`
    // (x, y and z). Returns this.
    Vec4& ClampTinyToNothing(float length);
    // 0x004A86D0: any of the four components differs (or is NaN).
    bool operator!=(const Vec4& other) const;
    // 0x0040AED0: all four components equal (NaN is unequal).
    bool operator==(const Vec4& other) const;

    // Removes the part of this vector that lies along `normal` (all four components are adjusted by
    // dot(xyz, normal.xyz) * normal). With a unit-length normal this projects onto the plane.
    Vec4& FlattenOntoPlane(const Vec4& normal);
    // Turn this vector into the plane with unit normal `normal`, keeping its length (x, y, z; w becomes 0).
    // If it points straight along the normal, (-normal.z, normal.x, -normal.y) is used as the direction.
    Vec4& AlignToPlane(const Vec4& normal);
    // Point along `direction` (assumed unit length), keeping this vector's length. w is scaled too.
    Vec4& ChangeHeading(const Vec4& direction);
    // Keep only the part along unit vector `axis`: axis * dot(this, axis). w is scaled too.
    Vec4& ProjectOnto(const Vec4& axis);
    // Debug output: "[x y z w]".
    void EchoOut() const;
    // The x, y or z component with the largest magnitude (ties favour x, then y); `axis` gets 0, 1 or 2.
    float DominantAxisIndex(int* axis) const;

    float MagnitudeSquared() const;
    float LenOf() const;
    // Scales x, y, z to length 1. A zero-length vector is left unchanged.
    Vec4& MakeUnit();
    // 0x0042BBF0: x, y and z scaled to `length` (left alone at length 0 or not a number). Returns this.
    Vec4* SetLength(float length);

    // Rotate about the Y axis by 1, 2 or 3 quarter turns (other values do nothing).
    Vec4& TurnQuarterAboutY(int quarter_turns);
    // Unit conversions applied to x, y, z (w is unchanged).
    void DegreeToRadianConv();
    void ConvertToDegrees();
    void FeetAsInches();
    // x = -x, y = -z, z = y. Purpose not identified yet (looks like a coordinate-system conversion).
    void NegateXRotateX90();

    // Turn by `angle` radians about the X / Y axis (the FPU's sine and cosine; w unchanged) - 0x00420A50 / 0x00420A90
    // (src/game/mth/quat.cpp).
    Vec4& RotateAboutX(float angle);
    Vec4& RotateAboutY(float angle);
    // 0x004882D0 (vector_rotate_z.cpp): the same about the Z axis.
    Vec4& RotateAboutZ(float angle);
    // 0x00421440: turn by `angle` radians about `axis` (through a quaternion: w becomes what QuatRotation::SpinBy leaves).
    Vec4& RotateAboutAxis(const Vec4& axis, float angle);

    // 0x00434200 (vector_transform.cpp): this = this * `matrix`, all four components (w too: a full 4x4 product,
    // each sum from the w term back to the x / y ones). Returns this.
    Vec4& MultiplyBy(const Mat4& matrix);
};

Vec4 operator+(const Vec4& a, const Vec4& b);
Vec4 operator-(const Vec4& a, const Vec4& b);
Vec4 operator*(const Vec4& v, float scale);
Vec4 operator*(float scale, const Vec4& v);
Vec4 operator-(const Vec4& v);

float DotVec(const Vec4& a, const Vec4& b);
// Cross product of the x, y, z parts; w of the result is 0.
Vec4 CrossVec(const Vec4& a, const Vec4& b);

// v times 1 / divisor (all four components).
Vec4 operator/(const Vec4& v, float divisor);
// 0x00423680: the distance between two points (x, y, z: z, x, then y squared and added; unrounded).
double GetDistance(const Vec4& a, const Vec4& b);
// Whether a and b are more than `degrees` apart (false if either is shorter than 0.5).
bool IsAngleBeyond(const Vec4& a, const Vec4& b, float degrees);

// 0x0041A820 (cdecl): the squared distance between the points a and b (x87: (dx * dx + dz * dz) + dy * dy, not
// rounded to float).
double DistanceSquared(const Vec4& a, const Vec4& b);

// The angle between a and b in degrees (0 to 180; a zero vector counts as itself).
double ReadTurnAngle(const Vec4& a, const Vec4& b);
// The angle in radians (-pi to pi) from a to b seen along `axis`: both projected onto the plane across it,
// negative when b is clockwise of a.
double GetTurnAroundAxis(const Vec4& a, const Vec4& b, const Vec4& axis);

// A pair of points. Name not confirmed yet: only its constructor (0x00402600) has been seen so far.
struct Seg {
    Vec4 start;
    Vec4 end;

    Seg() {} // (both ends left unset)
    Seg(const Vec4& from, const Vec4& to);

    // Squared distance from `point` to the closest point on the segment (x, y, z only). If `offset` is given
    // it receives point - closest point, including w.
    float DistanceSqrToPoint(const Vec4& point, Vec4* offset = nullptr) const;

    // Squared distance between this segment and `other` (x, y, z only). The closest points are
    // start + s * (end - start) on this segment and other.start + t * (other.end - other.start) on the other;
    // s and t (both in [0, 1]) are written out when the pointers are given. Returned as double: the original
    // leaves it in ST0 unrounded and some callers compare it before storing (ours is computed in floats).
    double DistanceSqrToLine(const Seg& other, float* s = nullptr, float* t = nullptr) const;
};

// Closest points between lines `a` and `b`: point_a = a.start + s * (a.end - a.start), point_b likewise with
// t. With `clamp`, s and t are limited to [0, 1] (segments instead of infinite lines). Returns false, leaving
// the outputs untouched, if either line is shorter than 1e-5 on every axis or they are (nearly) parallel.
bool ClosestPointsBetweenLines(const Seg& a, const Seg& b, Vec4& point_a, Vec4& point_b, float& s,
                               float& t, bool clamp);

} // namespace Mth
