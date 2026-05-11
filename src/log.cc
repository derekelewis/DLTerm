#include "log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace dlterm::log {
namespace {

struct Filter {
  Level default_level = Level::kInfo;
  // Sorted by category name; longest-prefix wins at lookup time.
  std::map<std::string, Level, std::less<>> per_category;
};

struct State {
  std::mutex mu;
  Filter filter;
  std::ofstream file_sink;
  bool initialized = false;
  bool init_in_progress = false;
  std::vector<Entry> ring;
  std::size_t ring_capacity = 512;
  std::size_t ring_head = 0;
  bool ring_full = false;
};

State& Global() {
  static State s;
  return s;
}

Level ParseLevel(std::string_view s, Level fallback) {
  // Case-insensitive ASCII compare (one-shot lowercase copy).
  std::string lc;
  lc.reserve(s.size());
  for (char c : s) {
    lc.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c);
  }
  if (lc == "trace") return Level::kTrace;
  if (lc == "debug") return Level::kDebug;
  if (lc == "info") return Level::kInfo;
  if (lc == "warn" || lc == "warning") return Level::kWarn;
  if (lc == "error") return Level::kError;
  return fallback;
}

Filter ParseFilter(std::string_view spec) {
  Filter f;
  if (spec.empty()) return f;
  while (!spec.empty()) {
    auto comma = spec.find(',');
    std::string_view tok = spec.substr(0, comma);
    while (!tok.empty() && tok.front() == ' ') tok.remove_prefix(1);
    while (!tok.empty() && tok.back() == ' ') tok.remove_suffix(1);
    if (!tok.empty()) {
      auto eq = tok.find('=');
      if (eq == std::string_view::npos) {
        f.default_level = ParseLevel(tok, f.default_level);
      } else {
        std::string cat{tok.substr(0, eq)};
        Level lv = ParseLevel(tok.substr(eq + 1), f.default_level);
        f.per_category[std::move(cat)] = lv;
      }
    }
    if (comma == std::string_view::npos) break;
    spec.remove_prefix(comma + 1);
  }
  return f;
}

bool MatchesPrefix(std::string_view category, std::string_view filter) {
  if (filter.empty()) return true;
  if (category == filter) return true;
  return category.size() > filter.size() &&
         category.starts_with(filter) && category[filter.size()] == '.';
}

Level EffectiveLevel(const Filter& f, std::string_view category) {
  Level best = f.default_level;
  std::size_t best_len = 0;
  bool matched = false;
  for (const auto& [cat, lv] : f.per_category) {
    if (MatchesPrefix(category, cat) && (!matched || cat.size() >= best_len)) {
      best = lv;
      best_len = cat.size();
      matched = true;
    }
  }
  return best;
}

long long NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(
             system_clock::now().time_since_epoch())
      .count();
}

void FormatTimestamp(long long ms, char* out, std::size_t n) {
  const auto secs = ms / 1000;
  const auto sub_ms = static_cast<int>(ms % 1000);
  const std::time_t t = static_cast<std::time_t>(secs);
  std::tm tm{};
  localtime_r(&t, &tm);
  std::snprintf(out, n, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min,
                tm.tm_sec, sub_ms);
}

void EnsureInitLocked(State& s) {
  if (s.initialized || s.init_in_progress) return;
  s.init_in_progress = true;
  if (const char* env = std::getenv("DLTERM_LOG"); env && *env) {
    s.filter = ParseFilter(env);
  }
  if (const char* path = std::getenv("DLTERM_LOG_FILE"); path && *path) {
    s.file_sink.open(path, std::ios::out | std::ios::app);
  }
  s.initialized = true;
  s.init_in_progress = false;
}

void PushRingLocked(State& s, Entry entry) {
  if (s.ring.size() < s.ring_capacity) {
    s.ring.push_back(std::move(entry));
    return;
  }
  s.ring[s.ring_head] = std::move(entry);
  s.ring_head = (s.ring_head + 1) % s.ring_capacity;
  s.ring_full = true;
}

}  // namespace

void Init() {
  auto& s = Global();
  std::lock_guard lock(s.mu);
  EnsureInitLocked(s);
}

void Shutdown() {
  auto& s = Global();
  std::lock_guard lock(s.mu);
  if (s.file_sink.is_open()) s.file_sink.close();
  s.initialized = false;
  s.filter = Filter{};
  s.ring.clear();
  s.ring_head = 0;
  s.ring_full = false;
}

void SetFilterForTesting(std::string_view spec) {
  auto& s = Global();
  std::lock_guard lock(s.mu);
  s.filter = ParseFilter(spec);
  s.initialized = true;
}

bool Enabled(Level level, std::string_view category) {
  auto& s = Global();
  std::lock_guard lock(s.mu);
  EnsureInitLocked(s);
  return static_cast<int>(level) >=
         static_cast<int>(EffectiveLevel(s.filter, category));
}

std::string_view LevelName(Level level) {
  switch (level) {
    case Level::kTrace: return "TRACE";
    case Level::kDebug: return "DEBUG";
    case Level::kInfo:  return "INFO ";
    case Level::kWarn:  return "WARN ";
    case Level::kError: return "ERROR";
  }
  return "?    ";
}

void Emit(Level level, std::string_view category, std::string_view message) {
  auto& s = Global();
  std::lock_guard lock(s.mu);
  EnsureInitLocked(s);
  if (static_cast<int>(level) <
      static_cast<int>(EffectiveLevel(s.filter, category))) {
    return;
  }
  const long long now = NowMs();
  char ts[16];
  FormatTimestamp(now, ts, sizeof ts);
  std::string line;
  line.reserve(message.size() + 64);
  line.append(ts);
  line.push_back(' ');
  const auto lname = LevelName(level);
  line.append(lname.data(), lname.size());
  line.append(" [");
  line.append(category);
  line.append("] ");
  line.append(message);
  line.push_back('\n');
  std::fwrite(line.data(), 1, line.size(), stderr);
  if (s.file_sink.is_open()) {
    s.file_sink.write(line.data(), static_cast<std::streamsize>(line.size()));
    s.file_sink.flush();
  }
  PushRingLocked(s, Entry{now, level, std::string{category},
                          std::string{message}});
}

std::vector<Entry> RecentEntries(std::size_t max) {
  auto& s = Global();
  std::lock_guard lock(s.mu);
  std::vector<Entry> out;
  if (s.ring.empty()) return out;
  const std::size_t total = s.ring_full ? s.ring_capacity : s.ring.size();
  const std::size_t take = std::min(max, total);
  // Oldest entry is at ring_head when full; at index 0 otherwise.
  const std::size_t oldest = s.ring_full ? s.ring_head : 0;
  const std::size_t skip = total - take;
  out.reserve(take);
  for (std::size_t i = 0; i < take; ++i) {
    out.push_back(s.ring[(oldest + skip + i) % s.ring_capacity]);
  }
  return out;
}

void FailCheck(const char* file, int line, const char* expr,
               std::string_view message) {
  std::fprintf(stderr, "DLTERM_CHECK failed: %s at %s:%d\n", expr, file, line);
  if (!message.empty()) {
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
  }
  std::abort();
}

}  // namespace dlterm::log
