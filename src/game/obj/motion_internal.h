#pragma once

// Shared pieces of the motion component's source files (motion_*.cpp): checksums, the original's x87 helpers,
// and the calls into its code that has no source here.

#include <bit>
#include <cstdint>

#include "game/obj/motion.h"

namespace Obj::MotionDetail {

// --- checksums ---
constexpr uint32_t kName = 0xA1DC81F9;        // Name
constexpr uint32_t kOff = 0xD443A2BC;         // Off
constexpr uint32_t kAmp = 0xC9FDE32C;         // Amp
constexpr uint32_t kFreq = 0xA80BEA4A;        // Freq
constexpr uint32_t kIps = 0xA18B8F32;         // ips
constexpr uint32_t kFps = 0xAAD7C80F;         // fps
constexpr uint32_t kMph = 0x2CE7EE02;         // mph
constexpr uint32_t kLink = 0xC953660E;        // link
constexpr uint32_t kSpeed = 0xF0D90109;       // speed
constexpr uint32_t kGravity = 0xA5E2DA58;     // gravity
constexpr uint32_t kColDistAbove = 0xECC2C699; // col_dist_above
constexpr uint32_t kColDistBelow = 0x5C4AE2F9; // col_dist_below
constexpr uint32_t kHeading = 0xFD4BC03E;     // heading
constexpr uint32_t kUseCurrentHeading = 0xFECFFE49; // use_current_heading
constexpr uint32_t kLandHeight = 0xA1810C27;  // land_height
constexpr uint32_t kMaxCoords = 0x26AF9DC9;   // FLAG_MAX_COORDS: a vector in 3ds Max's coordinates
constexpr uint32_t kObjMoveToNode = 0x8819DD8B; // Obj_MoveToNode
constexpr uint32_t kLockObj = 0x5CB5CB7E;     // the lockobj component

// --- the original's arithmetic ---
constexpr float Bits(uint32_t bits) { return std::bit_cast<float>(bits); }

// x87 FSIN, as the original takes its sines.
inline double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
inline int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

} // namespace Obj::MotionDetail
