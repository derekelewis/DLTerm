#include "layout.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace dlterm {
namespace {

constexpr int64_t kInf = std::numeric_limits<int64_t>::max() / 4;

int64_t Cube(int64_t x) { return x * x * x; }

bool IsBlankLine(std::string_view line) {
  for (char c : line) {
    if (c != ' ' && c != '\t' && c != '\r') return false;
  }
  return true;
}

std::vector<std::string_view> SplitParagraphs(std::string_view text) {
  std::vector<std::string_view> paragraphs;
  std::size_t pos = 0;
  std::size_t line_start = 0;
  std::size_t para_start = 0;
  bool in_paragraph = false;

  auto flush = [&](std::size_t para_end) {
    if (in_paragraph) {
      paragraphs.push_back(text.substr(para_start, para_end - para_start));
      in_paragraph = false;
    }
  };

  while (pos <= text.size()) {
    if (pos == text.size() || text[pos] == '\n') {
      std::string_view line = text.substr(line_start, pos - line_start);
      if (IsBlankLine(line)) {
        flush(line_start);
      } else if (!in_paragraph) {
        para_start = line_start;
        in_paragraph = true;
      }
      line_start = pos + 1;
    }
    ++pos;
  }
  flush(text.size());
  return paragraphs;
}

std::vector<std::string_view> Tokenize(std::string_view text) {
  std::vector<std::string_view> words;
  std::size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() &&
           std::isspace(static_cast<unsigned char>(text[i]))) {
      ++i;
    }
    std::size_t s = i;
    while (i < text.size() &&
           !std::isspace(static_cast<unsigned char>(text[i]))) {
      ++i;
    }
    if (i > s) words.push_back(text.substr(s, i - s));
  }
  return words;
}

}  // namespace

std::vector<std::size_t> OptimalBreaks(
    std::span<const std::string_view> words, std::size_t line_cols) {
  const std::size_t n = words.size();
  if (n == 0) return {0};

  std::vector<int64_t> dp(n + 1, kInf);
  std::vector<std::size_t> parent(n + 1, 0);
  dp[0] = 0;

  const int64_t L = static_cast<int64_t>(line_cols);

  for (std::size_t j = 1; j <= n; ++j) {
    int64_t width = 0;
    for (std::size_t i = j; i-- > 0;) {
      width += static_cast<int64_t>(words[i].size());
      if (i + 1 < j) width += 1;

      if (width > L) {
        // A single word longer than the line is force-placed on its own line
        // with no penalty (a negative cube would corrupt the DP).
        if (i + 1 == j && dp[i] < kInf && dp[i] < dp[j]) {
          dp[j] = dp[i];
          parent[j] = i;
        }
        break;
      }

      const int64_t line_cost = (j == n) ? 0 : Cube(L - width);
      if (dp[i] < kInf && dp[i] + line_cost < dp[j]) {
        dp[j] = dp[i] + line_cost;
        parent[j] = i;
      }
    }
  }

  std::vector<std::size_t> breaks;
  for (std::size_t k = n;; k = parent[k]) {
    breaks.push_back(k);
    if (k == 0) break;
  }
  std::reverse(breaks.begin(), breaks.end());
  return breaks;
}

std::vector<std::string> LayoutText(std::string_view text,
                                    std::size_t line_cols) {
  std::vector<std::string> lines;
  if (line_cols == 0) return lines;

  const auto paragraphs = SplitParagraphs(text);
  for (std::size_t p = 0; p < paragraphs.size(); ++p) {
    const auto words = Tokenize(paragraphs[p]);
    const auto breaks = OptimalBreaks(words, line_cols);
    for (std::size_t k = 0; k + 1 < breaks.size(); ++k) {
      std::string line;
      for (std::size_t w = breaks[k]; w < breaks[k + 1]; ++w) {
        if (w > breaks[k]) line.push_back(' ');
        line.append(words[w]);
      }
      lines.push_back(std::move(line));
    }
    if (p + 1 < paragraphs.size()) {
      lines.emplace_back();
    }
  }
  return lines;
}

}  // namespace dlterm

#ifdef DLTERM_LAYOUT_SELFTEST
#include <cstdio>

namespace {

int64_t Cube(int64_t x) { return x * x * x; }

int64_t LayoutCost(std::span<const std::string_view> words, std::size_t L,
                   std::span<const std::size_t> breaks) {
  if (breaks.size() < 2) return 0;
  int64_t total = 0;
  for (std::size_t k = 0; k + 2 < breaks.size(); ++k) {
    std::size_t width = 0;
    for (std::size_t w = breaks[k]; w < breaks[k + 1]; ++w) {
      if (w > breaks[k]) width += 1;
      width += words[w].size();
    }
    int64_t slack = static_cast<int64_t>(L) - static_cast<int64_t>(width);
    if (slack < 0) slack = 0;
    total += Cube(slack);
  }
  return total;
}

int64_t GreedyCost(std::span<const std::string_view> words, std::size_t L) {
  std::vector<std::size_t> breaks{0};
  std::size_t i = 0;
  while (i < words.size()) {
    std::size_t width = words[i].size();
    std::size_t j = i + 1;
    while (j < words.size() && width + 1 + words[j].size() <= L) {
      width += 1 + words[j].size();
      ++j;
    }
    breaks.push_back(j);
    i = j;
  }
  return LayoutCost(words, L, breaks);
}

}  // namespace

int main() {
  using std::string_view;
  const std::vector<string_view> words = {
      "Lorem",       "ipsum",   "dolor",  "sit",      "amet,",
      "consectetur", "adipiscing", "elit,", "sed",      "do",
      "eiusmod",     "tempor",  "incididunt", "ut",     "labore",
      "et",          "dolore",  "magna",  "aliqua."};

  bool ok = true;
  for (std::size_t L : {20u, 30u, 40u, 60u}) {
    auto breaks = dlterm::OptimalBreaks(words, L);
    int64_t opt = LayoutCost(words, L, breaks);
    int64_t greedy = GreedyCost(words, L);
    std::printf("L=%2zu  optimal=%6lld  greedy=%6lld  %s\n", L,
                static_cast<long long>(opt), static_cast<long long>(greedy),
                opt <= greedy ? "OK" : "FAIL");
    if (opt > greedy) ok = false;
  }
  return ok ? 0 : 1;
}
#endif
