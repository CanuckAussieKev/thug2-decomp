#include "engine/core/dbg.h"

namespace debug {

namespace {
OutputSink& output_sink()
{
    static OutputSink sink = nullptr;
    return sink;
}
} // namespace

void set_output_sink(OutputSink sink) { output_sink() = sink; }

// 0x00401C30 (printf, dbg.h: formatting, then this)
void output_text(const char* text)
{
    if (const OutputSink sink = output_sink()) {
        sink(text);
    }
}

} // namespace debug
