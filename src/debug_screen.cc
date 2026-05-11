#include "debug_screen.h"

#include <cstddef>
#include <format>
#include <string>
#include <vector>

#include "diagnostics.h"
#include "log.h"

namespace dlterm {
namespace {

constexpr std::size_t kMaxLogTail = 80;

}  // namespace

void DebugScreen::OnFrame(ScreenContext& ctx) {
  const std::size_t cols = BodyCols(ctx.metrics);
  if (cols == 0) {
    block_.SetLines({});
    return;
  }
  const auto snap = diag::Get();
  std::vector<std::string> lines;
  lines.push_back(TruncateToCols("DEBUG  press ESC to leave", cols));
  lines.emplace_back();
  lines.push_back(TruncateToCols(
      std::format("SCREEN          {}",
                   snap.current_screen.empty() ? "(none)" : snap.current_screen),
      cols));
  lines.push_back(TruncateToCols(
      std::format("TWS             {} {}:{} client_id={}",
                   snap.tws_connected ? "CONNECTED" : "disconnected",
                   snap.tws_host.empty() ? "?" : snap.tws_host,
                   snap.tws_port, snap.tws_client_id),
      cols));
  lines.push_back(TruncateToCols(
      std::format("FRAME           last={:.2f}ms  avg={:.2f}ms  count={}",
                   snap.frame_last_ms, snap.frame_avg_ms, snap.frame_count),
      cols));
  lines.emplace_back();
  lines.push_back(TruncateToCols("--- log tail ---", cols));

  // Compute how many log lines fit in the remaining body rows.
  const int body_rows = ctx.metrics.rows - kBarReservedRows;
  const int header_rows = static_cast<int>(lines.size());
  const int log_rows = body_rows - header_rows;
  if (log_rows > 0) {
    const std::size_t want = std::min<std::size_t>(
        kMaxLogTail, static_cast<std::size_t>(log_rows));
    auto entries = log::RecentEntries(want);
    for (const auto& e : entries) {
      lines.push_back(TruncateToCols(
          std::format("[{}] {} {}", log::LevelName(e.level), e.category,
                       e.message),
          cols));
    }
  }
  block_.SetLines(std::move(lines));
}

bool DebugScreen::OnEvent(const SDL_Event& /*e*/, ScreenContext& /*ctx*/) {
  return false;
}

}  // namespace dlterm
