#pragma once

// NOT ORIGINAL (the layout is the original's) - the object-update stream (message 0x23, wire/message_ids.h): the
// skaters' states as a packed bit stream (util::PackedBitBuffer), written by a client for its skater (to the host)
// and by the host for every skater (to each client); read back by the other side and measured by the host's length
// reader.
//
//   a client's update:  its clock (32 bits), then one skater's fields
//   the host's update:  a mask of the skaters present (8 bits, one per player slot), then for each present one the
//                       time (16 bits) and its fields
//   fields:             the field flags (9 bits), then for each flag set: the position x / y / z and the angles
//                       (16 bits each, signed; the angles' flags are the bits from kObjectUpdateAngleFieldBase up, in
//                       units of 1/4096 radian), the state (4 bits: the state and flag74; 6 bits; two flag bits),
//                       the controls (8 bits: 5, then 3 of a component), a word (32 bits).
namespace online {

constexpr int kObjectUpdateBytes = 0x400;          // the most an update stream takes (the intake / output buffer)
constexpr int kObjectUpdateClockBits = 32;         // a client's update: its app clock first
constexpr int kObjectUpdateTimeBits = 16;          // the host's update: each skater's time
constexpr int kObjectUpdateFieldBits = 9;          // the field flags
constexpr int kObjectUpdatePoseFields = 6;         // the position and the angles
constexpr int kObjectUpdatePoseBits = 16;          // each position or angle field (signed)
constexpr unsigned kObjectUpdateAngleFieldBase = 8u; // the three angle fields are the flag bits from this one up
constexpr double kObjectUpdateAngleScale = 4096.0; // angle units to a radian
constexpr int kObjectUpdateHasState = 0x40;        // the flags of the optional fields
constexpr int kObjectUpdateHasControls = 0x80;
constexpr int kObjectUpdateHasWord = 0x100;
constexpr int kObjectUpdateStateMask = 7;          // the state field's low bits: the state
constexpr int kObjectUpdateFlag74Shift = 7;        // (the reader's shift of the flag74 bit: always 0, the original's)
constexpr int kObjectUpdateFlag74Mask = 0x80;      // (the writer's)
constexpr int kObjectUpdateValue7cBits = 6;        // the state field's second part
constexpr int kObjectUpdateControlsBits = 5;       // the controls field's first part
constexpr int kObjectUpdateControlsFieldBits = 8;  // the controls field in all
constexpr int kObjectUpdateWordBits = 32;

} // namespace online
