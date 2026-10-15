#include "game/mth/matrix.h"

#include <cmath>

#include "hook/hook.h"

namespace Mth {

// 0x00404090
Matrix& Matrix::operator=(const Matrix& other)
{
    for (int i = 0; i < 4; ++i)
        row[i] = other.row[i];
    return *this;
}

// 0x00404080
Vector& Matrix::operator[](int index)
{
    return row[index];
}

// 0x00404DD0
Matrix& CreateRotateXMatrix(Matrix& m, float angle)
{
    float s = std::sin(angle);
    float c = std::cos(angle);
    m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, c, s, 0.0f};
    m.row[2] = {0.0f, -s, c, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return m;
}

// 0x00404E20
Matrix& CreateRotateYMatrix(Matrix& m, float angle)
{
    float s = std::sin(angle);
    float c = std::cos(angle);
    m.row[0] = {c, 0.0f, -s, 0.0f};
    m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m.row[2] = {s, 0.0f, c, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return m;
}

// 0x00404E70
Matrix& CreateRotateZMatrix(Matrix& m, float angle)
{
    float s = std::sin(angle);
    float c = std::cos(angle);
    m.row[0] = {c, s, 0.0f, 0.0f};
    m.row[1] = {-s, c, 0.0f, 0.0f};
    m.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return m;
}

Matrix& Multiply(Matrix& out, const Matrix& a, const Matrix& b)
{
    Matrix result;
    for (int i = 0; i < 4; ++i) {
        const float* ra = &a.row[i].x;
        float* rr = &result.row[i].x;
        for (int j = 0; j < 4; ++j)
            rr[j] = ra[0] * (&b.row[0].x)[j] + ra[1] * (&b.row[1].x)[j] + ra[2] * (&b.row[2].x)[j] +
                    ra[3] * (&b.row[3].x)[j];
    }
    out = result;
    return out;
}

// The originals build the rotation and call the statically linked D3DXMatrixMultiply(this, &rotation, this).

// 0x00405670
Matrix& Matrix::RotateXLocal(float angle)
{
    Matrix rotation;
    return Multiply(*this, CreateRotateXMatrix(rotation, angle), *this);
}

// 0x00405710
Matrix& Matrix::RotateYLocal(float angle)
{
    Matrix rotation;
    return Multiply(*this, CreateRotateYMatrix(rotation, angle), *this);
}

// 0x004057B0
Matrix& Matrix::RotateZLocal(float angle)
{
    Matrix rotation;
    return Multiply(*this, CreateRotateZMatrix(rotation, angle), *this);
}

} // namespace Mth

REPLACE(0x00405670, &Mth::Matrix::RotateXLocal);
REPLACE(0x00405710, &Mth::Matrix::RotateYLocal);
REPLACE(0x004057B0, &Mth::Matrix::RotateZLocal);

// operator= is __thiscall returning `this`; the rotation builders are cdecl and return the matrix pointer.
REPLACE(0x00404080, &Mth::Matrix::operator[]);
REPLACE(0x00404090, &Mth::Matrix::operator=);
REPLACE(0x00404DD0, Mth::CreateRotateXMatrix);
REPLACE(0x00404E20, Mth::CreateRotateYMatrix);
REPLACE(0x00404E70, Mth::CreateRotateZMatrix);
