#include "base/core/log_sink.h"

namespace core {

namespace {
ErrorLineSink& error_line_sink()
{
    static ErrorLineSink sink = nullptr;
    return sink;
}
} // namespace

void set_error_line_sink(ErrorLineSink sink) { error_line_sink() = sink; }

void error_line(const char* line)
{
    if (const ErrorLineSink sink = error_line_sink()) {
        sink(line);
    }
}

} // namespace core
