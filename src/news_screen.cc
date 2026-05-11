#include "news_screen.h"

#include <cstddef>
#include <ctime>
#include <format>
#include <string>
#include <utility>

#include "layout.h"

namespace dlterm {

namespace {

constexpr std::size_t kMaxHeadlines = 200;
constexpr Uint64 kFlashMs = 5000;
constexpr std::time_t kFlashMaxAgeSec = 300;
constexpr int kFeedHeaderRows = 2;

std::string FormatLocalTime(std::time_t ts) {
  std::tm tm{};
  if (ts <= 0) return "-- --:--:--";
  localtime_r(&ts, &tm);
  return std::format("{:02d}-{:02d} {:02d}:{:02d}:{:02d}", tm.tm_mon + 1,
                     tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

std::string CleanHeadline(std::string_view raw) {
  if (!raw.empty() && raw.front() == '{') {
    const auto end = raw.find('}');
    if (end != std::string_view::npos) {
      raw.remove_prefix(end + 1);
      if (!raw.empty() && raw.front() == '!') raw.remove_prefix(1);
    }
  }
  while (!raw.empty() && raw.front() == ' ') raw.remove_prefix(1);
  return std::string{raw};
}

std::string StripHtml(std::string_view html) {
  std::string out;
  out.reserve(html.size());
  bool in_tag = false;
  for (std::size_t i = 0; i < html.size(); ++i) {
    char c = html[i];
    if (in_tag) {
      if (c == '>') in_tag = false;
      continue;
    }
    if (c == '<') {
      in_tag = true;
      continue;
    }
    if (c == '&') {
      const auto semi = html.find(';', i + 1);
      if (semi != std::string_view::npos && semi - i <= 8) {
        std::string_view ent = html.substr(i + 1, semi - i - 1);
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "#39" || ent == "apos") out.push_back('\'');
        else if (ent == "nbsp") out.push_back(' ');
        else out.append(html.substr(i, semi - i + 1));
        i = semi;
        continue;
      }
    }
    out.push_back(c);
  }
  std::string collapsed;
  collapsed.reserve(out.size());
  int newline_run = 0;
  bool last_space = false;
  for (char c : out) {
    if (c == '\n') {
      ++newline_run;
      last_space = false;
      continue;
    }
    if (newline_run > 0) {
      collapsed.push_back('\n');
      if (newline_run >= 2) collapsed.push_back('\n');
      newline_run = 0;
      last_space = false;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      if (last_space) continue;
      collapsed.push_back(' ');
      last_space = true;
    } else {
      collapsed.push_back(c);
      last_space = false;
    }
  }
  return collapsed;
}

}  // namespace

NewsScreen::NewsScreen(std::string symbol) : symbol_(std::move(symbol)) {
  label_ = symbol_.empty() ? "MARKET" : symbol_;
}

void NewsScreen::OnEnter(ScreenContext& ctx) {
  if (!ctx.client) return;
  if (symbol_.empty()) {
    ctx.client->SubscribeGeneralNews();
  } else {
    ctx.client->SubscribeNews(symbol_);
  }
}

void NewsScreen::OnExit(ScreenContext& ctx) {
  if (ctx.client) ctx.client->CancelNews();
}

void NewsScreen::InsertSorted(NewsHeadline h) {
  auto it = headlines_.begin();
  while (it != headlines_.end() && it->ts >= h.ts) ++it;
  headlines_.insert(it, std::move(h));
}

void NewsScreen::OnFrame(ScreenContext& ctx) {
  if (!ctx.client || !ctx.client->IsConnected()) return;

  // Headlines.
  std::vector<NewsHeadline> fresh = ctx.client->DrainHeadlines();
  if (!fresh.empty()) {
    const Uint64 now_ms = SDL_GetTicks();
    const std::time_t now_wall = std::time(nullptr);
    for (auto& h : fresh) {
      // Only flash genuinely fresh items. The live tick stream replays
      // recent realtime-provider (e.g. DJ-RT, DJ-RTG) copies of days-old
      // historical items on subscribe; those are new to us but not actually
      // live, so we gate on wall-clock age of the headline itself.
      const bool is_live =
          h.ts > 0 && now_wall - h.ts <= kFlashMaxAgeSec;
      if (loaded_ && !h.article_id.empty() && is_live) {
        flash_until_ms_[h.article_id] = now_ms + kFlashMs;
      }
      InsertSorted(std::move(h));
    }
    while (headlines_.size() > kMaxHeadlines) headlines_.pop_back();
    if (hovered_idx_ >= 0 &&
        static_cast<std::size_t>(hovered_idx_) >= headlines_.size()) {
      hovered_idx_ = -1;
    }
  }

  // Statuses (loaded flag).
  if (ctx.frame_statuses) {
    for (const auto& msg : *ctx.frame_statuses) {
      if (!loaded_ && (msg.starts_with("Historical loaded:") ||
                       msg.starts_with("Streaming BroadTape:"))) {
        loaded_ = true;
      }
    }
  }

  // Article body for the reader sub-mode.
  if (reading_) {
    auto art = ctx.client->DrainArticle();
    if (art && art->request_id == reader_req_id_) {
      reader_body_text_ = StripHtml(art->text);
      reader_received_ = true;
      reader_block_.SetBody(LayoutText(reader_body_text_, BodyCols(ctx.metrics)));
    }
  }

  // Always rebuild the feed lines (fast; we render every frame anyway).
  RebuildFeedLines(BodyCols(ctx.metrics), SDL_GetTicks());
}

void NewsScreen::RebuildFeedLines(std::size_t cols, Uint64 now_ms) {
  std::vector<std::string> lines;
  if (cols == 0) {
    feed_block_.SetLines({});
    return;
  }
  std::string header = loaded_
      ? std::format("NEWS  {}  ({} headlines)", label_, headlines_.size())
      : std::format("NEWS  {}  (loading...)", label_);
  lines.push_back(TruncateToCols(header, cols));
  lines.emplace_back();

  constexpr std::size_t kTimeW = 14;
  constexpr std::size_t kGap1 = 2;
  constexpr std::size_t kProvW = 6;
  constexpr std::size_t kGap2 = 2;
  constexpr std::size_t kPrefixW = kTimeW + kGap1 + kProvW + kGap2;
  const std::size_t headline_w = cols > kPrefixW ? cols - kPrefixW : 0;

  std::vector<int> bold_rows;
  for (std::size_t i = 0; i < headlines_.size(); ++i) {
    const auto& h = headlines_[i];
    std::string prov = h.provider.size() > kProvW
                           ? h.provider.substr(0, kProvW)
                           : h.provider;
    prov.resize(kProvW, ' ');
    std::string headline = CleanHeadline(h.headline);
    std::string row;
    row.reserve(cols);
    row.append(FormatLocalTime(h.ts));
    row.append(kGap1, ' ');
    row.append(prov);
    row.append(kGap2, ' ');
    if (headline_w > 0) row.append(TruncateToCols(headline, headline_w));
    lines.push_back(std::move(row));

    if (!h.article_id.empty()) {
      auto it = flash_until_ms_.find(h.article_id);
      if (it != flash_until_ms_.end() && it->second > now_ms) {
        bold_rows.push_back(kFeedHeaderRows + static_cast<int>(i));
      }
    }
  }
  feed_block_.SetLines(std::move(lines));
  feed_block_.SetBoldRows(std::move(bold_rows));
}

void NewsScreen::Render(ScreenContext& ctx) {
  if (reading_) {
    reader_block_.Render(ctx);
    return;
  }
  feed_block_.Render(ctx);
  if (hovered_idx_ >= 0 &&
      static_cast<std::size_t>(hovered_idx_) < headlines_.size() &&
      ctx.render) {
    DrawHoverRect(*ctx.render, ctx.metrics, kFeedHeaderRows + hovered_idx_);
  }
}

bool NewsScreen::OnEvent(const SDL_Event& e, ScreenContext& ctx) {
  if (e.type == SDL_EVENT_WINDOW_RESIZED ||
      e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
    if (reading_) {
      reader_block_.SetBody(
          LayoutText(reader_body_text_, BodyCols(ctx.metrics)));
    }
    return false;
  }
  if (reading_) return OnReaderEvent(e, ctx);
  return OnFeedEvent(e, ctx);
}

bool NewsScreen::OnFeedEvent(const SDL_Event& e, ScreenContext& ctx) {
  if (e.type == SDL_EVENT_MOUSE_MOTION) {
    const int body_row = MouseYToBodyRow(ctx.pixel_density, ctx.metrics, e.motion.y);
    const int idx = body_row - kFeedHeaderRows;
    if (idx < 0 || static_cast<std::size_t>(idx) >= headlines_.size()) {
      hovered_idx_ = -1;
    } else {
      hovered_idx_ = idx;
    }
    return false;
  }
  if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      e.button.button == SDL_BUTTON_LEFT) {
    const int body_row = MouseYToBodyRow(ctx.pixel_density, ctx.metrics, e.button.y);
    const int idx = body_row - kFeedHeaderRows;
    if (idx < 0 || static_cast<std::size_t>(idx) >= headlines_.size()) {
      return false;
    }
    const auto& h = headlines_[idx];
    if (!ctx.client) return true;
    const int req = ctx.client->RequestArticle(h.provider, h.article_id);
    if (req == 0) {
      if (ctx.bar) {
        ctx.bar->status = "Not connected";
        ctx.bar->last_change_ms = SDL_GetTicks();
      }
      return true;
    }
    reading_ = true;
    reader_req_id_ = req;
    reader_received_ = false;
    reader_title_ = CleanHeadline(h.headline);
    reader_subtitle_ = std::format("{}  {}", h.provider, FormatLocalTime(h.ts));
    reader_body_text_ = "(loading article...)";
    const std::size_t cols = BodyCols(ctx.metrics);
    reader_block_.SetHeader({TruncateToCols(reader_title_, cols),
                              TruncateToCols(reader_subtitle_, cols)});
    reader_block_.SetBody(LayoutText(reader_body_text_, cols));
    hovered_idx_ = -1;
    return true;
  }
  return false;
}

bool NewsScreen::OnReaderEvent(const SDL_Event& e, ScreenContext& ctx) {
  if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
    reading_ = false;
    reader_body_text_.clear();
    reader_block_.SetHeader({});
    reader_block_.SetBody({});
    return true;
  }
  return reader_block_.OnScrollEvent(e, ctx.metrics);
}

}  // namespace dlterm
