#include "game/mth/matrix.h"

#include <cmath>

#include "game/core/dbg.h"
#include "game/mth/math.h"
#include "hook/hook.h"
#include "hook/shadow.h"

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

// 0x0041A6A0 / 0x0041A6D0 / 0x0041A700 (the originals: D3DXMatrixMultiply(this, this, &rotation))
Matrix& Matrix::RotateX(float angle)
{
    Matrix rotation;
    return Multiply(*this, *this, CreateRotateXMatrix(rotation, angle));
}

Matrix& Matrix::RotateY(float angle)
{
    Matrix rotation;
    return Multiply(*this, *this, CreateRotateYMatrix(rotation, angle));
}

Matrix& Matrix::RotateZ(float angle)
{
    Matrix rotation;
    return Multiply(*this, *this, CreateRotateZMatrix(rotation, angle));
}

// 0x0041A640
Matrix& Matrix::Ident()
{
    row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return *this;
}

// 0x0041BA30
Matrix& Matrix::Scale(const Vector& scale)
{
    for (Vector& r : row) {
        r.x *= scale.x;
        r.y *= scale.y;
        r.z *= scale.z;
        r.w *= scale.w;
    }
    return *this;
}

// 0x0048A230
Matrix& Matrix::Translate(const Vector& offset)
{
    row[3].x += offset.x;
    row[3].y += offset.y;
    row[3].z += offset.z;
    row[3].w += offset.w;
    return *this;
}

// 0x0041F470. Each component's sum in the original's order (x87, rounded once): the w row's term and the x row's
// first, then z's, then y's (for x: w, z, y, then x's term last).
Vector operator*(const Vector& v, const Matrix& m)
{
    auto term = [&](int r, int c) { return static_cast<double>((&m.row[r].x)[c]) * (&v.x)[r]; };
    Vector out;
    out.w = static_cast<float>(term(3, 3) + term(0, 3) + term(2, 3) + term(1, 3));
    out.z = static_cast<float>(term(3, 2) + term(0, 2) + term(2, 2) + term(1, 2));
    out.y = static_cast<float>(term(3, 1) + term(0, 1) + term(2, 1) + term(1, 1));
    out.x = static_cast<float>(term(3, 0) + term(2, 0) + term(1, 0) + term(0, 0));
    return out;
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

// 0x00404C60
Matrix& CreateRotateMatrix(Matrix& m, const Vector& axis, float angle)
{
    float x = axis.x, y = axis.y, z = axis.z;
    float length = std::sqrt(x * x + y * y + z * z);
    if (length > 0.0f) {
        float scale = 1.0f / length;
        x *= scale;
        y *= scale;
        z *= scale;
    }

    float t = 1.0f - std::cos(angle);
    float s = std::sin(angle);
    m.row[0] = {1.0f - (1.0f - x * x) * t, x * y * t + z * s, z * x * t - y * s, 0.0f};
    m.row[1] = {x * y * t - z * s, 1.0f - (1.0f - y * y) * t, z * y * t + x * s, 0.0f};
    m.row[2] = {z * x * t + y * s, z * y * t - x * s, 1.0f - (1.0f - z * z) * t, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return m;
}

// 0x00404100 (the original calls the statically linked D3DXMatrixMultiply)
Matrix& Matrix::operator=(const MatrixProduct& product)
{
    return Multiply(*this, *product.left, *product.right);
}

// 0x004062D0
float GetAngleInPlane(const Matrix& m, const Vector& v, int ref_axis, int normal_axis)
{
    const Vector& normal = m.row[normal_axis];
    const Vector& ref = m.row[ref_axis];

    Vector in_plane = v;
    in_plane.ProjectToPlane(normal);
    if (in_plane.z * in_plane.z + in_plane.x * in_plane.x + in_plane.y * in_plane.y < 0.0001f)
        return 0.0f;
    in_plane.Normalize();

    float cosine = in_plane.z * ref.z + in_plane.y * ref.y + in_plane.x * ref.x;
    if (!(cosine <= 1.0f)) // NaN also becomes 1, as in the original
        cosine = 1.0f;
    else if (cosine < -1.0f)
        cosine = -1.0f;
    float angle = std::acos(cosine);

    // Which way round: compare ref x in_plane with the plane normal on its largest axis.
    Vector cross{ref.y * in_plane.z - ref.z * in_plane.y, ref.z * in_plane.x - ref.x * in_plane.z,
                 ref.x * in_plane.y - ref.y * in_plane.x, 0.0f};
    int axis;
    bool cross_positive = cross.GetMaxAxis(&axis) > 0.0f;
    bool normal_positive = (&normal.x)[axis] > 0.0f;
    return cross_positive == normal_positive ? angle : -angle;
}

// 0x00405310
void Matrix::GetRotationAxisAngle(Vector& axis, float& angle) const
{
    const float trace = (row[1].y + row[0].x + row[2].z) - 1.0f; // 2 cos(angle)
    const float x = row[1].z - row[2].y;
    const float y = row[2].x - row[0].z;
    const float z = row[0].y - row[1].x;
    const float length = std::sqrt(y * y + x * x + z * z); // 2 sin(angle)

    if (length <= 0.0f) {
        axis = {0.0f, 0.0f, 0.0f, 1.0f};
    } else {
        float inv = 1.0f / length;
        axis = {x * inv, y * inv, z * inv, inv};
    }
    angle = std::atan2(length, trace);

    if (!(length <= 0.01f) || !(trace <= 0.0f))
        return;

    // Near 180 degrees the differences above vanish: rebuild the axis from the largest diagonal element.
    // (Nested exactly as in the original so NaNs choose the same case.)
    const float m00 = row[0].x, m11 = row[1].y, m22 = row[2].z;
    const Vector y_largest{row[1].x + row[0].y, m11 + 1.0f + m11 + 1.0f, row[1].z + row[2].y, 1.0f};
    const Vector x_largest{m00 + 1.0f + m00 + 1.0f, row[1].x + row[0].y, row[0].z + row[2].x, 1.0f};
    const Vector z_largest{row[0].z + row[2].x, row[1].z + row[2].y, m22 + 1.0f + m22 + 1.0f, 1.0f};
    if (m00 <= m11)
        axis = m22 < m11 ? y_largest : z_largest;
    else
        axis = m22 < m00 ? x_largest : z_largest;
    axis.Normalize();
}

// 0x00405850
void Matrix::RotateLocal(const Vector& angles)
{
    if (angles.x != 0.0f)
        RotateXLocal(angles.x);
    if (angles.y != 0.0f)
        RotateYLocal(angles.y);
    if (angles.z != 0.0f)
        RotateZLocal(angles.z);
}

// 0x00404BA0
void Matrix::Print() const
{
    Dbg::Printf("-----------------\n");
    for (const Vector& r : row)
        Dbg::Printf("%f %f %f %f\n", r.x, r.y, r.z, r.w);
    Dbg::Printf("-----------------\n");
}

// 0x00405170
Matrix& Matrix::OrthoNormalizeAbout(int axis)
{
    int b_index = axis + 1;
    if (b_index == 3)
        b_index = 0;
    int c_index = b_index + 1;
    if (c_index == 3)
        c_index = 0;
    const Vector& a = row[axis];
    Vector& b = row[b_index];
    Vector& c = row[c_index];

    // b = c x a and c = a x b, written out in the original's operand order.
    auto rebuild_b = [&] {
        b = {a.z * c.y - a.y * c.z, c.z * a.x - a.z * c.x, a.y * c.x - c.y * a.x, 0.0f};
        b.Normalize();
    };
    auto rebuild_c = [&] {
        c = {b.z * a.y - b.y * a.z, b.x * a.z - a.x * b.z, a.x * b.y - b.x * a.y, 0.0f};
        c.Normalize();
    };

    float c_dot_a = c.x * a.x + c.y * a.y + c.z * a.z;
    float a_dot_b = a.x * b.x + a.y * b.y + a.z * b.z;
    if (std::fabs(c_dot_a) < std::fabs(a_dot_b)) {
        rebuild_b();
        rebuild_c();
    } else {
        rebuild_c();
        rebuild_b();
    }
    return *this;
}

// 0x00404EC0
Matrix& CreateRotateMatrix(Matrix& m, int axis, float angle)
{
    float s = std::sin(angle);
    float c = std::cos(angle);
    int a = (axis + 1) % 3;
    int b = (axis + 2) % 3;
    m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    (&m.row[a].x)[a] = c;
    (&m.row[a].x)[b] = s;
    (&m.row[b].x)[a] = -s;
    (&m.row[b].x)[b] = c;
    return m;
}

// 0x00404F50
Matrix& CreateLookAtMatrix(Matrix& m, const Vector& eye, const Vector& target, const Vector& up)
{
    float fx = target.x - eye.x, fy = target.y - eye.y, fz = target.z - eye.z;
    float length = std::sqrt(fy * fy + fx * fx + fz * fz);
    if (length > 0.0f) {
        float scale = 1.0f / length;
        fx *= scale;
        fy *= scale;
        fz *= scale;
    }
    // right = forward x up, true_up = right x forward
    float rx = fy * up.z - fz * up.y, ry = fz * up.x - fx * up.z, rz = fx * up.y - fy * up.x;
    float ux = ry * fz - rz * fy, uy = rz * fx - rx * fz, uz = rx * fy - ry * fx;

    m.row[0] = {rx, ux, fx, 0.0f};
    m.row[1] = {ry, uy, fy, 0.0f};
    m.row[2] = {rz, uz, fz, 0.0f};
    m.row[3] = {-(rz * eye.z + ry * eye.y + rx * eye.x), -(uz * eye.z + uy * eye.y + ux * eye.x),
                -(fy * eye.y + fx * eye.x + fz * eye.z), 1.0f};
    return m;
}

// 0x00405080
Matrix& CreateOrthographicMatrix(Matrix& m, float width, float height, float near_z, float far_z)
{
    m.row[0] = {2.0f / width, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, 2.0f / height, 0.0f, 0.0f};
    m.row[2] = {0.0f, 0.0f, 2.0f / (far_z - near_z), 0.0f};
    m.row[3] = {0.0f, 0.0f, -((near_z + far_z) / (far_z - near_z)), 1.0f};
    return m;
}

// 0x004050F0
Matrix& CreatePerspectiveMatrix(Matrix& m, float fov, float aspect, float near_z, float far_z)
{
    float q = far_z / (far_z - near_z);
    float cot = std::cos(fov * 0.5f) / std::sin(fov * 0.5f);
    m.row[0] = {aspect * cot, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, cot, 0.0f, 0.0f};
    m.row[2] = {0.0f, 0.0f, q, 1.0f};
    m.row[3] = {0.0f, 0.0f, -(q * near_z), 0.0f};
    return m;
}

// 0x00405950
Matrix& Matrix::Rotate(const Vector& axis, float angle)
{
    Matrix rotation;
    return Multiply(*this, *this, CreateRotateMatrix(rotation, axis, angle));
}

// 0x005772A0 (the original: D3DXMatrixMultiply(this, &rotation, this))
Matrix& Matrix::RotateLocal(const Vector& axis, float angle)
{
    Matrix rotation;
    return Multiply(*this, CreateRotateMatrix(rotation, axis, angle), *this);
}

// 0x00404120
Matrix& Matrix::operator*=(float scale)
{
    if (scale != 1.0f) {
        for (Vector& r : row)
            r *= scale;
    }
    return *this;
}

// 0x004048D0
Matrix& Matrix::SetFromEulerAngles(float x, float y, float z)
{
    float cx = std::cos(x), sx = std::sin(x);
    float cy = std::cos(y), sy = std::sin(y);
    float cz = std::cos(z), sz = std::sin(z);
    row[0] = {cz * cy, -(sz * cy), -sy, 0.0f};
    row[1] = {sz * cx - sy * sx * cz, cz * cx + sy * sx * sz, -(cy * sx), 0.0f};
    row[2] = {sz * sx + cx * sy * cz, cz * sx - cx * sy * sz, cy * cx, 0.0f};
    row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return *this;
}

namespace {

// Element (r, c), treating the four row vectors as a 4x4 array.
float At(const Matrix& m, int r, int c)
{
    return (&m.row[r].x)[c];
}

// Determinant of the 3x3 matrix left after removing row `skip_row` and column `skip_col`, expanded along its
// first row in the same order as the original: a(ei - fh) - b(di - fg) + c(dh - eg).
float Minor(const Matrix& m, int skip_row, int skip_col)
{
    int r[3], c[3];
    for (int i = 0, n = 0; i < 4; ++i)
        if (i != skip_row)
            r[n++] = i;
    for (int i = 0, n = 0; i < 4; ++i)
        if (i != skip_col)
            c[n++] = i;
    float a = At(m, r[0], c[0]), b = At(m, r[0], c[1]), cc = At(m, r[0], c[2]);
    float d = At(m, r[1], c[0]), e = At(m, r[1], c[1]), f = At(m, r[1], c[2]);
    float g = At(m, r[2], c[0]), h = At(m, r[2], c[1]), i = At(m, r[2], c[2]);
    return a * (e * i - f * h) - b * (d * i - f * g) + cc * (d * h - e * g);
}

} // namespace

// 0x004041E0
float Matrix::Determinant() const
{
    return At(*this, 0, 0) * Minor(*this, 0, 0) - At(*this, 0, 1) * Minor(*this, 0, 1) +
           At(*this, 0, 2) * Minor(*this, 0, 2) - At(*this, 0, 3) * Minor(*this, 0, 3);
}

// 0x004043A0
Matrix& Matrix::Adjugate(const Matrix& source)
{
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float cofactor = Minor(source, c, r);
            (&row[r].x)[c] = ((r + c) & 1) ? -cofactor : cofactor;
        }
    }
    return *this;
}

// 0x00405600
Matrix& Matrix::Invert()
{
    Matrix adjugate;
    adjugate.Adjugate(*this);
    float determinant = Determinant();
    if (determinant != 1.0f)
        adjugate *= 1.0f / determinant;
    *this = adjugate;
    return *this;
}

// 0x00404AB0
Vector& Matrix::GetEulerAngles(Vector& out) const
{
    float sin_y = row[0].z;
    if (!(sin_y <= 1.0f)) // also catches NaN, as the original does
        sin_y = 1.0f;
    else if (sin_y < -1.0f)
        sin_y = -1.0f;

    float y = -std::asin(sin_y);
    float cos_y = std::cos(y);
    float x, z;
    if (std::fabs(cos_y) > 0.005f) {
        float inv = 1.0f / cos_y;
        float neg_inv = -1.0f / cos_y;
        x = FastAtan2(neg_inv * row[1].z, inv * row[2].z);
        z = FastAtan2(neg_inv * row[0].y, inv * row[0].x);
    } else {
        // Gimbal lock: X and Z rotate about the same axis, so put it all in Z.
        x = 0.0f;
        z = FastAtan2(row[1].x, row[1].y);
    }
    out = {x, y, z, 1.0f};
    return out;
}

} // namespace Mth

REPLACE_SHADOW(0x004041E0, &Mth::Matrix::Determinant);
REPLACE(0x004043A0, &Mth::Matrix::Adjugate);
REPLACE(0x00404AB0, &Mth::Matrix::GetEulerAngles);
REPLACE(0x00405600, &Mth::Matrix::Invert);
REPLACE(0x00404120, &Mth::Matrix::operator*=);
REPLACE(0x004048D0, &Mth::Matrix::SetFromEulerAngles);
REPLACE(0x00404C60, static_cast<Mth::Matrix& (*)(Mth::Matrix&, const Mth::Vector&, float)>(&Mth::CreateRotateMatrix));
REPLACE(0x00404100, static_cast<Mth::Matrix& (Mth::Matrix::*)(const Mth::MatrixProduct&)>(&Mth::Matrix::operator=));
REPLACE(0x00405170, &Mth::Matrix::OrthoNormalizeAbout);
REPLACE(0x00405310, &Mth::Matrix::GetRotationAxisAngle);
REPLACE_SHADOW(0x004062D0, Mth::GetAngleInPlane);
REPLACE(0x00405850, static_cast<void (Mth::Matrix::*)(const Mth::Vector&)>(&Mth::Matrix::RotateLocal));
REPLACE(0x005772A0, static_cast<Mth::Matrix& (Mth::Matrix::*)(const Mth::Vector&, float)>(&Mth::Matrix::RotateLocal));
REPLACE(0x00404BA0, &Mth::Matrix::Print);
REPLACE(0x00404EC0,static_cast<Mth::Matrix& (*)(Mth::Matrix&, int, float)>(&Mth::CreateRotateMatrix));
REPLACE(0x00404F50, Mth::CreateLookAtMatrix);
REPLACE(0x00405080, Mth::CreateOrthographicMatrix);
REPLACE(0x004050F0, Mth::CreatePerspectiveMatrix);
REPLACE(0x00405950, &Mth::Matrix::Rotate);
REPLACE(0x00405670, &Mth::Matrix::RotateXLocal);
REPLACE(0x00405710, &Mth::Matrix::RotateYLocal);
REPLACE(0x004057B0, &Mth::Matrix::RotateZLocal);
REPLACE(0x0041A6A0, &Mth::Matrix::RotateX);
REPLACE(0x0041A6D0, &Mth::Matrix::RotateY);
REPLACE(0x0041A700, &Mth::Matrix::RotateZ);
REPLACE(0x0041A640, &Mth::Matrix::Ident);
REPLACE(0x0041BA30, &Mth::Matrix::Scale);
REPLACE(0x0048A230, &Mth::Matrix::Translate);
REPLACE(0x0041F470, static_cast<Mth::Vector (*)(const Mth::Vector&, const Mth::Matrix&)>(&Mth::operator*));

// operator= is __thiscall returning `this`; the rotation builders are cdecl and return the matrix pointer.
REPLACE(0x00404080, &Mth::Matrix::operator[]);
REPLACE(0x00404090, static_cast<Mth::Matrix& (Mth::Matrix::*)(const Mth::Matrix&)>(&Mth::Matrix::operator=));
REPLACE(0x00404DD0, Mth::CreateRotateXMatrix);
REPLACE(0x00404E20, Mth::CreateRotateYMatrix);
REPLACE(0x00404E70, Mth::CreateRotateZMatrix);
