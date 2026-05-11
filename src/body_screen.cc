#include "body_screen.h"

#include <cctype>

#include "layout.h"

namespace dlterm {

namespace {

std::size_t CountWords(std::string_view text) {
  std::size_t count = 0;
  bool in_word = false;
  for (char c : text) {
    const bool ws = std::isspace(static_cast<unsigned char>(c));
    if (in_word && ws) {
      ++count;
      in_word = false;
    } else if (!in_word && !ws) {
      in_word = true;
    }
  }
  if (in_word) ++count;
  return count;
}

}  // namespace

void BodyScreen::Layout(ScreenContext& ctx) {
  block_.SetLines(LayoutText(text_, BodyCols(ctx.metrics)));
}

std::size_t BodyScreen::BodyWordCount() const {
  return CountWords(text_);
}

}  // namespace dlterm
