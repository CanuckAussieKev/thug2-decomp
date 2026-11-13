#pragma once

#include <cstdint>

// NOT ORIGINAL - where a frame's time goes (`[debug] frame_profile=1` in thug2-decomp.ini; off by default, and then
// nothing is measured). The main loop's phases and the parts inside them are named scopes; each loop's times are added
// up and every five seconds the log gets a breakdown ("frame-profile: ..."): the loops drawn and skipped, the drawn
// frames' spacing and work (average and 99th percentile) and each phase's milliseconds. Each loop is also a line of
// thug2-decomp-frames.jsonl next to the exe (tools/game/frame_bench.ts reads it).
// `frame_profile=2` also samples the main thread about once a millisecond (x64 only): each sample's call stack, by
// function (its start, from the code's unwind data) and by the phase it was in, written to
// thug2-decomp-samples.txt every ten seconds - module, RVA, own and inclusive samples (frame_bench.ts --samples names
// them with the PDB).
// Only the main thread's scopes count (the thread that called loop_begin first).
namespace frame_profile {

// The setting: 0 off, 1 phases, 2 phases and samples.
int level();
inline bool on() { return level() != 0; }

// A phase's slot (made on its first use; at most 48 names - more are ignored).
int slot(const char* name);
// Milliseconds measured elsewhere (the device's time for the frame) added to this loop's `slot`.
void add_ms(int slot, double milliseconds);

// The time between construction and destruction added to the named phase (nothing when off, or off the main thread).
class Scope {
public:
    explicit Scope(const char* name);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

private:
    int slot_ = -1;
    int outer_ = -1;
    int64_t start_ = 0;
};

// The main loop: a loop starts (the thread calling it is the main thread from then on) ...
void loop_begin();
// ... and ends: whether it was drawn, whether a loading screen was up.
void loop_end(bool drawn, bool loading);

} // namespace frame_profile
