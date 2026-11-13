#pragma once

// NOT ORIGINAL - thug2_format_check's checks of the surface-map files (formats/surface_map_format.h;
// format_check_surface.cpp).
namespace format_check_surface {

// The content hash against FNV-1a's published values, a file written and read back, the same bytes the TypeScript
// writer makes (tools/pbr_maps/pbr_file.test.ts holds the same sample), broken files refused. Returns the failures.
int run();

} // namespace format_check_surface
