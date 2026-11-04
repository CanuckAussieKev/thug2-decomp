#pragma once

#include <d3d9.h>

#include "base/core/raw_view.h"

// Direct3D's matrix type over the game's: 16 floats, row by row (maths::Mat4::data()), as D3D lays a D3DMATRIX out.
namespace d3d {

inline const D3DMATRIX* matrix(const float* values) { return raw::as<const D3DMATRIX>(values); }
inline D3DMATRIX* matrix(float* values) { return raw::as<D3DMATRIX>(values); }

} // namespace d3d
