#ifndef DLTERM_SRC_SCREEN_FACTORY_H_
#define DLTERM_SRC_SCREEN_FACTORY_H_

#include <memory>

#include "function_bar.h"
#include "screen.h"

namespace dlterm {

// Builds a Screen for the given command result. Returns nullptr if the
// command isn't a screen-creating verb (caller falls through to its
// own status-line / no-op handling).
std::unique_ptr<Screen> MakeScreenFor(const CommandResult& result);

}  // namespace dlterm

#endif  // DLTERM_SRC_SCREEN_FACTORY_H_
