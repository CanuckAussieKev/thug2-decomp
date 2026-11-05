#pragma once

// NOT ORIGINAL - what level_editor_chains.cpp's pieces share (the chains' state, the graph and the helpers more than
// one piece uses). They were the one file's anonymous namespace; here they are in `internal`, with a using-directive,
// so every piece sees them as it saw them there.

#include <algorithm>
#include <string>
#include <vector>
#include <gsl/pointers>

namespace frontend::level_editor::chains {

namespace internal {

bool same_text(const std::string& a, const char* b);

template <typename T>
void sort_by_name(std::vector<T>* list)
{
    std::sort(list->begin(), list->end(), [](const T& a, const T& b) { return a.name < b.name; });
}

} // namespace internal

// What the source file's anonymous namespace held that its pieces share: visible as it was.
using namespace internal;

} // namespace frontend::level_editor::chains
