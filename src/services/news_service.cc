#include "services/news_service.h"

#include <ctime>
#include <string>
#include <utility>

#include "Contract.h"
#include "EClient.h"
#include "NewsProvider.h"

namespace dlterm {

namespace {

constexpr int kHistoricalCount = 50;

constexpr const char* kBroadtapeSources[] = {
    "BRFG", "BRFUPDN", "DJ", "DJNL", "BZ", "DJTOP", "FLY",
};

std::time_t ParseTwsHistoricalTime(const std::string& s) {
  std::tm tm{};
  if (!strptime(s.c_str(), "%Y-%m-%d %H:%M:%S", &tm)) return 0;
  return timegm(&tm);
}

}  // namespace

Contract NewsService::MakeStockContract(const std::string& symbol) {
  Contract c;
  c.symbol = symbol;
  c.secType = "STK";
  c.exchange = "SMART";
  c.currency = "USD";
  return c;
}

void NewsService::EnsureProvidersRequested() { want_providers_ = true; }

void NewsService::OnReady() {
  if (want_providers_ && !providers_requested_) {
    providers_requested_ = true;
    if (ctx_.client) ctx_.client->reqNewsProviders();
  }
  if (stage_ == Stage::kResolving && details_req_id_ != 0) {
    Contract c = MakeStockContract(symbol_);
    if (ctx_.client) ctx_.client->reqContractDetails(details_req_id_, c);
  }
  if (stage_ == Stage::kBroadtapeDiscover && !pending_broadtape_probes_.empty()) {
    auto queued = std::move(pending_broadtape_probes_);
    pending_broadtape_probes_.clear();
    for (auto& [id, contract] : queued) {
      if (ctx_.client) ctx_.client->reqContractDetails(id, contract);
    }
  }
}

void NewsService::OnReset() {
  stage_ = Stage::kIdle;
  symbol_.clear();
  con_id_ = 0;
  details_req_id_ = 0;
  hist_req_id_ = 0;
  ticker_stream_req_id_ = 0;
  hist_count_ = 0;
  seen_article_ids_.clear();
  providers_requested_ = false;
  want_providers_ = false;
  provider_codes_.clear();
  broadtape_probe_ids_.clear();
  broadtape_contracts_.clear();
  broadtape_stream_req_ids_.clear();
  pending_broadtape_probes_.clear();
  headlines_.clear();
  article_.reset();
}

void NewsService::StartTickerSession(const std::string& symbol) {
  CancelActive();
  headlines_.clear();
  seen_article_ids_.clear();
  symbol_ = symbol;
  con_id_ = 0;
  hist_count_ = 0;
  stage_ = Stage::kResolving;
  details_req_id_ = ctx_.NextReqId();
  Contract c = MakeStockContract(symbol);
  if (ctx_.client) ctx_.client->reqContractDetails(details_req_id_, c);
}

void NewsService::StartGeneralSession() {
  CancelActive();
  headlines_.clear();
  seen_article_ids_.clear();
  symbol_ = "MARKET";
  hist_count_ = 0;
  broadtape_contracts_.clear();
  broadtape_probe_ids_.clear();
  broadtape_stream_req_ids_.clear();
  stage_ = Stage::kBroadtapeDiscover;

  std::vector<std::pair<int, Contract>> probes;
  for (const char* src : kBroadtapeSources) {
    const int id = ctx_.NextReqId();
    broadtape_probe_ids_.insert(id);
    Contract bt;
    bt.symbol = std::string(src) + ":" + src + "_ALL";
    bt.secType = "NEWS";
    bt.exchange = src;
    probes.emplace_back(id, std::move(bt));
  }
  if (ctx_.client) {
    for (auto& [id, contract] : probes) {
      ctx_.client->reqContractDetails(id, contract);
    }
  } else {
    pending_broadtape_probes_ = std::move(probes);
  }
}

void NewsService::CancelActive() {
  std::set<int> stream_ids = broadtape_stream_req_ids_;
  const int ticker_id = ticker_stream_req_id_;
  stage_ = Stage::kIdle;
  symbol_.clear();
  con_id_ = 0;
  details_req_id_ = 0;
  hist_req_id_ = 0;
  ticker_stream_req_id_ = 0;
  broadtape_probe_ids_.clear();
  broadtape_contracts_.clear();
  broadtape_stream_req_ids_.clear();
  pending_broadtape_probes_.clear();
  seen_article_ids_.clear();
  if (!ctx_.client) return;
  for (int id : stream_ids) ctx_.client->cancelMktData(id);
  if (ticker_id != 0) ctx_.client->cancelMktData(ticker_id);
}

int NewsService::RequestArticle(const std::string& provider,
                                 const std::string& article_id) {
  const int req_id = ctx_.NextReqId();
  if (ctx_.client) {
    ctx_.client->reqNewsArticle(req_id, provider, article_id,
                                 TagValueListSPtr());
  }
  return req_id;
}

std::vector<NewsHeadline> NewsService::DrainHeadlines() {
  std::vector<NewsHeadline> out;
  out.swap(headlines_);
  return out;
}

std::optional<ArticlePayload> NewsService::DrainArticle() {
  if (!article_.has_value()) return std::nullopt;
  auto out = std::move(article_);
  article_.reset();
  return out;
}

bool NewsService::OwnsReqId(int req_id) const {
  return req_id == details_req_id_ || req_id == hist_req_id_ ||
         req_id == ticker_stream_req_id_ ||
         broadtape_probe_ids_.contains(req_id) ||
         broadtape_stream_req_ids_.contains(req_id);
}

void NewsService::OnContractDetails(int req_id,
                                     const ContractDetails& details) {
  if (stage_ == Stage::kResolving && req_id == details_req_id_) {
    if (con_id_ == 0) con_id_ = details.contract.conId;
    return;
  }
  if (stage_ == Stage::kBroadtapeDiscover &&
      broadtape_probe_ids_.contains(req_id)) {
    Contract c = details.contract;
    const auto colon = c.symbol.find(':');
    if (colon != std::string::npos) c.exchange = c.symbol.substr(0, colon);
    broadtape_contracts_.push_back(std::move(c));
  }
}

void NewsService::OnContractDetailsEnd(int req_id) {
  if (stage_ == Stage::kResolving && req_id == details_req_id_) {
    if (con_id_ == 0) {
      ctx_.EmitStatus("No contract found for " + symbol_);
      stage_ = Stage::kIdle;
      return;
    }
    if (provider_codes_.empty()) {
      ctx_.EmitStatus(
          "No news providers available for this account "
          "(check IB news subscriptions)");
      stage_ = Stage::kIdle;
      return;
    }
    hist_req_id_ = ctx_.NextReqId();
    stage_ = Stage::kFetchingHist;
    const std::string codes = provider_codes_;
    const int hist_id = hist_req_id_;
    const int con_id_local = con_id_;
    if (ctx_.client) {
      ctx_.client->reqHistoricalNews(hist_id, con_id_local, codes, "", "",
                                      kHistoricalCount, TagValueListSPtr());
    }
    return;
  }
  if (stage_ == Stage::kBroadtapeDiscover &&
      broadtape_probe_ids_.contains(req_id)) {
    HandleBroadtapeProbeFinished(req_id);
  }
}

void NewsService::HandleBroadtapeProbeFinished(int req_id) {
  broadtape_probe_ids_.erase(req_id);
  if (!broadtape_probe_ids_.empty()) return;

  if (broadtape_contracts_.empty()) {
    ctx_.EmitStatus(
        "No BroadTape feed available; falling back to SPY historical");
    StartTickerSession("SPY");
    return;
  }

  std::string joined;
  std::vector<std::pair<int, Contract>> streams;
  for (const auto& c : broadtape_contracts_) {
    if (!joined.empty()) joined.push_back('+');
    joined.append(c.symbol);
    const int sid = ctx_.NextReqId();
    broadtape_stream_req_ids_.insert(sid);
    streams.emplace_back(sid, c);
  }
  stage_ = Stage::kBroadtapeStream;
  ctx_.EmitStatus("Streaming BroadTape: " + joined);
  if (ctx_.client) {
    for (auto& [id, c] : streams) {
      ctx_.client->reqMktData(id, c, "mdoff,292", false, false,
                               TagValueListSPtr());
    }
  }
}

void NewsService::OnHistoricalNews(int req_id, const std::string& time_str,
                                     const std::string& provider,
                                     const std::string& article_id,
                                     const std::string& headline) {
  if (req_id != hist_req_id_ || stage_ != Stage::kFetchingHist) return;
  if (!article_id.empty() && !seen_article_ids_.insert(article_id).second) {
    return;
  }
  NewsHeadline h;
  h.ts = ParseTwsHistoricalTime(time_str);
  h.provider = provider;
  h.article_id = article_id;
  h.headline = headline;
  headlines_.push_back(std::move(h));
  ++hist_count_;
}

void NewsService::OnHistoricalNewsEnd(int req_id) {
  if (req_id != hist_req_id_ || stage_ != Stage::kFetchingHist) return;
  ticker_stream_req_id_ = ctx_.NextReqId();
  stage_ = Stage::kStreamingTicker;
  Contract c = MakeStockContract(symbol_);
  const std::string codes =
      provider_codes_.empty() ? std::string("BRFG+BRFUPDN+DJNL")
                              : provider_codes_;
  const std::string ticks = "mdoff,292:" + codes;
  ctx_.EmitStatus("Historical loaded: " + std::to_string(hist_count_) +
                  " headlines, streaming live");
  if (ctx_.client) {
    ctx_.client->reqMktData(ticker_stream_req_id_, c, ticks, false, false,
                              TagValueListSPtr());
  }
}

void NewsService::OnTickNews(int req_id, time_t ts,
                              const std::string& provider,
                              const std::string& article_id,
                              const std::string& headline) {
  const bool ok = (stage_ == Stage::kBroadtapeStream &&
                   broadtape_stream_req_ids_.contains(req_id)) ||
                  (stage_ == Stage::kStreamingTicker &&
                   req_id == ticker_stream_req_id_);
  if (!ok) return;
  if (!article_id.empty() && !seen_article_ids_.insert(article_id).second) {
    return;
  }
  NewsHeadline h;
  h.ts = static_cast<std::time_t>(ts / 1000);
  h.provider = provider;
  h.article_id = article_id;
  h.headline = headline;
  headlines_.push_back(std::move(h));
}

void NewsService::OnNewsProviders(const std::vector<NewsProvider>& providers) {
  std::string joined;
  for (const auto& p : providers) {
    if (!joined.empty()) joined.push_back('+');
    joined.append(p.providerCode);
  }
  provider_codes_ = std::move(joined);
}

void NewsService::OnNewsArticle(int req_id, int article_type,
                                  const std::string& text) {
  ArticlePayload p;
  p.request_id = req_id;
  p.article_type = article_type;
  p.text = text;
  article_ = std::move(p);
}

bool NewsService::OnError(int req_id, int code) {
  if (code == 200 && stage_ == Stage::kBroadtapeDiscover &&
      broadtape_probe_ids_.contains(req_id)) {
    HandleBroadtapeProbeFinished(req_id);
    return true;
  }
  return false;
}

}  // namespace dlterm
