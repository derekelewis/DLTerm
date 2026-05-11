#include "splash_screen.h"

#include <cstdlib>
#include <ctime>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "ibkr_client.h"
#include "redact.h"

namespace dlterm {

namespace {

constexpr Uint64 kServerTimeReqIntervalMs = 5000;

// A `localtime`-style breakdown plus the zone abbreviation.
struct ZonedTime {
  std::tm tm{};
  std::string abbrev;
  long gmt_off_sec = 0;
};

// Convert a UTC time_t into the requested IANA zone. Uses the
// POSIX `TZ` env-var around `localtime_r` (single-threaded on the
// render thread; the splash is the only caller).
ZonedTime ToZone(std::time_t t, const char* zone) {
  const char* old_tz_raw = std::getenv("TZ");
  std::string saved_tz;
  bool had_tz = false;
  if (old_tz_raw) {
    saved_tz = old_tz_raw;
    had_tz = true;
  }
  setenv("TZ", zone, 1);
  tzset();

  ZonedTime z;
  localtime_r(&t, &z.tm);
  z.abbrev = z.tm.tm_zone ? z.tm.tm_zone : "";
  z.gmt_off_sec = z.tm.tm_gmtoff;

  if (had_tz) setenv("TZ", saved_tz.c_str(), 1);
  else unsetenv("TZ");
  tzset();
  return z;
}

// Returns true if the given local time falls within a regular trading
// session for the named exchange. Lightweight approximations — these
// don't model holidays or half-days.
bool NyseOpen(const std::tm& t) {
  const int wday = t.tm_wday;  // 0=Sun
  if (wday == 0 || wday == 6) return false;
  const int mins = t.tm_hour * 60 + t.tm_min;
  return mins >= (9 * 60 + 30) && mins < (16 * 60);
}
bool LseOpen(const std::tm& t) {
  const int wday = t.tm_wday;
  if (wday == 0 || wday == 6) return false;
  const int mins = t.tm_hour * 60 + t.tm_min;
  return mins >= (8 * 60) && mins < (16 * 60 + 30);
}
bool TseOpen(const std::tm& t) {
  const int wday = t.tm_wday;
  if (wday == 0 || wday == 6) return false;
  const int mins = t.tm_hour * 60 + t.tm_min;
  const bool morning = mins >= (9 * 60) && mins < (11 * 60 + 30);
  const bool afternoon = mins >= (12 * 60 + 30) && mins < (15 * 60);
  return morning || afternoon;
}

std::string ClockLine(std::string_view label, const ZonedTime& z,
                       bool market_open) {
  return std::format("  {:<10} {:02d}:{:02d}:{:02d} {:<4}  MARKET {}",
                     label, z.tm.tm_hour, z.tm.tm_min, z.tm.tm_sec, z.abbrev,
                     market_open ? "OPEN" : "CLOSED");
}

// "20260510 13:22:00 EST" -> "2026-05-10 13:22:00 EST"
std::string PrettyTwsConnectTime(const std::string& raw) {
  if (raw.size() < 8) return raw;
  std::string out;
  out.reserve(raw.size() + 2);
  out.append(raw, 0, 4);
  out.push_back('-');
  out.append(raw, 4, 2);
  out.push_back('-');
  out.append(raw, 6, 2);
  if (raw.size() > 8) out.append(raw, 8, std::string::npos);
  return out;
}

std::string JoinProviders(
    const std::vector<std::pair<std::string, std::string>>& providers,
    std::size_t budget_cols) {
  std::string out;
  for (const auto& [code, _] : providers) {
    const std::size_t added = (out.empty() ? 0 : 1) + code.size();
    if (out.size() + added > budget_cols) {
      out.append(" ...");
      break;
    }
    if (!out.empty()) out.push_back(' ');
    out.append(code);
  }
  return out;
}

}  // namespace

void SplashScreen::OnEnter(ScreenContext& ctx) {
  if (ctx.client && !ctx.client->IsConnected()) {
    if (!ctx.client->Connect(host_, port_, client_id_)) {
      if (ctx.bar) {
        ctx.bar->status = std::format("TWS connect failed ({}:{})", host_,
                                       port_);
        ctx.bar->last_change_ms = ctx.now_ms;
      }
    }
  }
  if (ctx.client && ctx.client->IsConnected()) {
    ctx.client->RequestServerTime();
    last_server_time_req_ms_ = ctx.now_ms;
  }
  Layout(ctx);
}

void SplashScreen::OnFrame(ScreenContext& ctx) {
  if (ctx.client && ctx.client->IsConnected() &&
      ctx.now_ms - last_server_time_req_ms_ >= kServerTimeReqIntervalMs) {
    ctx.client->RequestServerTime();
    last_server_time_req_ms_ = ctx.now_ms;
  }
  Layout(ctx);
}

bool SplashScreen::OnEvent(const SDL_Event& e, ScreenContext& ctx) {
  if (e.type == SDL_EVENT_WINDOW_RESIZED ||
      e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
    Layout(ctx);
  }
  return false;
}

void SplashScreen::Layout(ScreenContext& ctx) {
  const std::size_t cols = BodyCols(ctx.metrics);
  const std::time_t now = std::time(nullptr);

  const ZonedTime local = ToZone(now, "");
  const ZonedTime ny  = ToZone(now, "America/New_York");
  const ZonedTime lon = ToZone(now, "Europe/London");
  const ZonedTime tyo = ToZone(now, "Asia/Tokyo");

  const std::string today = std::format("{:04d}-{:02d}-{:02d}",
                                        local.tm.tm_year + 1900,
                                        local.tm.tm_mon + 1, local.tm.tm_mday);

  std::vector<std::string> lines;
  // Banner: "DLTERM 0.1" left, today's date right-aligned to col 72.
  {
    std::string banner = "DLTERM 0.1";
    constexpr std::size_t kBannerWidth = 72;
    if (banner.size() < kBannerWidth) {
      banner.append(kBannerWidth - banner.size() - today.size(), ' ');
    }
    banner.append(today);
    lines.push_back(TruncateToCols(banner, cols));
  }
  lines.emplace_back();

  // World clocks.
  lines.push_back("WORLD CLOCKS");
  lines.push_back(TruncateToCols(ClockLine("NEW YORK", ny, NyseOpen(ny.tm)),
                                  cols));
  lines.push_back(TruncateToCols(ClockLine("LONDON", lon, LseOpen(lon.tm)),
                                  cols));
  lines.push_back(TruncateToCols(ClockLine("TOKYO", tyo, TseOpen(tyo.tm)),
                                  cols));
  lines.emplace_back();

  // TWS connection block.
  lines.push_back("TWS CONNECTION");
  const bool connected = ctx.client && ctx.client->IsConnected();
  const std::string endpoint = std::format("{}:{} (client {})", host_, port_,
                                            client_id_);
  if (!connected) {
    lines.push_back(TruncateToCols(
        std::format("  {:<18}  {:<14} {}", "STATUS", "DISCONNECTED", endpoint),
        cols));
  } else {
    const InfoSnapshot info = ctx.client->SnapshotInfo();
    lines.push_back(TruncateToCols(
        std::format("  {:<18}  {:<14} {}", "STATUS", "CONNECTED", endpoint),
        cols));
    lines.push_back(TruncateToCols(
        std::format("  {:<18}  {}", "SERVER VERSION", info.server_version),
        cols));
    {
      std::string client_api;
      if (!info.client_api_version.empty()) {
        client_api = std::format("{}  (proto {}..{})",
                                  info.client_api_version,
                                  info.client_min_version,
                                  info.client_max_version);
      } else {
        client_api = std::format("proto {}..{}",
                                  info.client_min_version,
                                  info.client_max_version);
      }
      lines.push_back(TruncateToCols(
          std::format("  {:<18}  {}", "CLIENT API", client_api), cols));
    }
    if (!info.tws_connect_time.empty()) {
      lines.push_back(TruncateToCols(
          std::format("  {:<18}  {}", "CONNECTED SINCE",
                      PrettyTwsConnectTime(info.tws_connect_time)),
          cols));
    }
    if (info.server_time) {
      const ZonedTime svr = ToZone(*info.server_time, "");
      const long skew = static_cast<long>(*info.server_time - now);
      lines.push_back(TruncateToCols(
          std::format("  {:<18}  {:02d}:{:02d}:{:02d} {:<4}  (skew {:+d}s)",
                      "SERVER TIME", svr.tm.tm_hour, svr.tm.tm_min,
                      svr.tm.tm_sec, svr.abbrev, static_cast<int>(skew)),
          cols));
    }
    if (info.accounts.empty()) {
      lines.push_back(TruncateToCols(
          std::format("  {:<18}  (waiting...)", "ACCOUNTS"), cols));
    } else {
      std::string joined;
      for (const auto& a : info.accounts) {
        if (!joined.empty()) joined.push_back(' ');
        joined.append(MaybeRedactAccount(a));
      }
      lines.push_back(TruncateToCols(
          std::format("  {:<18}  {}", "ACCOUNTS", joined), cols));
    }
    if (info.news_providers.empty()) {
      lines.push_back(TruncateToCols(
          std::format("  {:<18}  (waiting...)", "NEWS PROVIDERS"), cols));
    } else {
      // Budget: total cols minus indent(2) + label(18) + gap(2) and tail "(N)".
      const std::string tail = std::format(" ({})", info.news_providers.size());
      const std::size_t left_pad = 2 + 18 + 2;
      const std::size_t budget = cols > left_pad + tail.size()
                                     ? cols - left_pad - tail.size()
                                     : 0;
      const std::string codes = JoinProviders(info.news_providers, budget);
      lines.push_back(TruncateToCols(
          std::format("  {:<18}  {}{}", "NEWS PROVIDERS", codes, tail),
          cols));
    }
  }
  lines.emplace_back();
  lines.push_back("Type a command above. ESC returns here.");

  block_.SetLines(std::move(lines));
}

}  // namespace dlterm
