#pragma once

// NOT ORIGINAL - what level_editor_triggers.cpp's pieces share (the trigger state, the records and the helpers more
// than one piece uses). They were the one file's anonymous namespace; here they are in `internal`, with a
// using-directive, so every piece sees them as it saw them there.

#include <cstdint>
#include <new>
#include <string>
#include <gsl/pointers>

namespace frontend::level_editor::triggers {

namespace internal {

constexpr float kHalf = 0.5F;                                    // half

constexpr float kMaxTriggerSize = 50000.0F;

bool same_text(const std::string& a, const std::string& b);

uint32_t get_checksums(const char* text);

uint32_t start_gap();

uint32_t end_gap();

uint32_t get_gap_id();

uint32_t get_score();

uint32_t get_text();

uint32_t current_node_name();

uint32_t kill_skater();

uint32_t kill_skater_water();

uint32_t get_teleport();

} // namespace internal

// What the source file's anonymous namespace held that its pieces share: visible as it was.
using namespace internal;

} // namespace frontend::level_editor::triggers
