#ifndef DLTERM_SRC_LOG_H_
#define DLTERM_SRC_LOG_H_

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dlterm::log {

enum class Level {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
};

// Reads DLTERM_LOG and DLTERM_LOG_FILE on first use; safe to call from
// multiple threads. Idempotent.
void Init();

// Drops file sink, clears ring buffer, forgets parsed env vars. Tests
// call this between cases.
void Shutdown();

// Force a specific filter (overrides env). Used by tests.
void SetFilterForTesting(std::string_view spec);

bool Enabled(Level level, std::string_view category);
void Emit(Level level, std::string_view category, std::string_view message);

template <typename... Args>
void EmitFormat(Level level, std::string_view category,
                std::format_string<Args...> fmt, Args&&... args) {
  if (!Enabled(level, category)) return;
  Emit(level, category, std::format(fmt, std::forward<Args>(args)...));
}

struct Entry {
  long long timestamp_ms = 0;
  Level level = Level::kInfo;
  std::string category;
  std::string message;
};

// Returns up to `max` most-recent entries (oldest first).
std::vector<Entry> RecentEntries(std::size_t max);
std::string_view LevelName(Level level);

[[noreturn]] void FailCheck(const char* file, int line, const char* expr,
                            std::string_view message);

}  // namespace dlterm::log

#define DLTERM_LOG_TRACE(cat, ...) \
  ::dlterm::log::EmitFormat(::dlterm::log::Level::kTrace, (cat), __VA_ARGS__)
#define DLTERM_LOG_DEBUG(cat, ...) \
  ::dlterm::log::EmitFormat(::dlterm::log::Level::kDebug, (cat), __VA_ARGS__)
#define DLTERM_LOG_INFO(cat, ...) \
  ::dlterm::log::EmitFormat(::dlterm::log::Level::kInfo, (cat), __VA_ARGS__)
#define DLTERM_LOG_WARN(cat, ...) \
  ::dlterm::log::EmitFormat(::dlterm::log::Level::kWarn, (cat), __VA_ARGS__)
#define DLTERM_LOG_ERROR(cat, ...) \
  ::dlterm::log::EmitFormat(::dlterm::log::Level::kError, (cat), __VA_ARGS__)

#define DLTERM_CHECK(cond)                                            \
  do {                                                                \
    if (!(cond)) {                                                    \
      ::dlterm::log::FailCheck(__FILE__, __LINE__, #cond,             \
                                std::string_view{});                   \
    }                                                                 \
  } while (0)

#ifdef NDEBUG
#define DLTERM_DCHECK(cond) ((void)0)
#else
#define DLTERM_DCHECK(cond) DLTERM_CHECK(cond)
#endif

#endif  // DLTERM_SRC_LOG_H_
