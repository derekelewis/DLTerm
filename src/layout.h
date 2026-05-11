#ifndef DLTERM_SRC_LAYOUT_H_
#define DLTERM_SRC_LAYOUT_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dlterm {

// Returns indices [0, l1, l2, ..., N] such that line k spans
// words[breaks[k] .. breaks[k+1]). Minimizes the cubic sum of trailing
// whitespace per line, excluding the last line, per Knuth-style optimal-fit.
std::vector<std::size_t> OptimalBreaks(
    std::span<const std::string_view> words, std::size_t line_cols);

// Splits text on blank lines into paragraphs, then per paragraph tokenizes on
// whitespace and runs OptimalBreaks. Returns a flat sequence of laid-out lines
// with a single empty string inserted between paragraphs.
std::vector<std::string> LayoutText(std::string_view text,
                                    std::size_t line_cols);

}  // namespace dlterm

#endif  // DLTERM_SRC_LAYOUT_H_
