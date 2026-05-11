#ifndef DLTERM_TESTS_SUPPORT_FAKE_IBKR_CLIENT_H_
#define DLTERM_TESTS_SUPPORT_FAKE_IBKR_CLIENT_H_

#include <optional>
#include <string>
#include <vector>

#include "ibkr_client.h"

namespace dlterm::test {

// In-memory IIbkrClient impl. Tests drive state directly with the
// Push*/Set* helpers; production-style code paths (Subscribe* /
// Drain* / Snapshot*) work just like the live client.
class FakeIbkrClient : public IIbkrClient {
 public:
  // ---- Test driver helpers ----
  void PushHeadline(NewsHeadline h) { headlines_.push_back(std::move(h)); }
  void SetArticleResponse(int request_id, std::string text,
                           int article_type = 0) {
    pending_article_ = ArticlePayload{request_id, article_type, std::move(text)};
  }
  void SetPortfolio(PortfolioSnapshot snap) { portfolio_ = std::move(snap); }
  void SetChart(ChartSnapshot snap) { chart_ = std::move(snap); }
  void SetQuote(QuoteSnapshot snap) { quote_ = std::move(snap); }
  void SetInfo(InfoSnapshot snap) { info_ = std::move(snap); }
  void EmitStatus(std::string msg) { status_.push_back(std::move(msg)); }
  void SetConnected(bool v) { connected_ = v; }

  // ---- Call counters ----
  int subscribe_news_calls = 0;
  int subscribe_general_calls = 0;
  int cancel_news_calls = 0;
  int request_article_calls = 0;
  int subscribe_portfolio_calls = 0;
  int unsubscribe_portfolio_calls = 0;
  int request_chart_calls = 0;
  int refresh_chart_calls = 0;
  int cancel_chart_calls = 0;
  int request_quote_calls = 0;
  int cancel_quote_calls = 0;
  int pump_calls = 0;
  int request_server_time_calls = 0;
  std::string last_news_symbol;
  std::string last_chart_symbol;
  ChartInstrument last_chart_instrument = ChartInstrument::kStock;
  std::string last_quote_symbol;
  ChartInstrument last_quote_instrument = ChartInstrument::kStock;
  std::string last_article_provider;
  std::string last_article_id;

  // ---- IIbkrClient overrides ----
  bool Connect(const std::string&, int, int) override {
    connected_ = true;
    return true;
  }
  void Disconnect() override { connected_ = false; }
  bool IsConnected() const override { return connected_; }

  int SubscribeNews(const std::string& symbol) override {
    if (!connected_) return 0;
    ++subscribe_news_calls;
    last_news_symbol = symbol;
    return ++next_req_id_;
  }
  void SubscribeGeneralNews() override {
    if (!connected_) return;
    ++subscribe_general_calls;
  }
  void CancelNews() override { ++cancel_news_calls; }

  int RequestArticle(const std::string& provider,
                      const std::string& article_id) override {
    if (!connected_) return 0;
    ++request_article_calls;
    last_article_provider = provider;
    last_article_id = article_id;
    const int req = ++next_req_id_;
    if (pending_article_) pending_article_->request_id = req;
    return req;
  }

  void SubscribePortfolio() override {
    if (!connected_) return;
    ++subscribe_portfolio_calls;
  }
  void UnsubscribePortfolio() override { ++unsubscribe_portfolio_calls; }
  PortfolioSnapshot SnapshotPortfolio() const override { return portfolio_; }

  int RequestChart(const std::string& symbol,
                    ChartInstrument kind = ChartInstrument::kStock) override {
    if (!connected_) return 0;
    ++request_chart_calls;
    last_chart_symbol = symbol;
    last_chart_instrument = kind;
    return ++next_req_id_;
  }
  void RefreshChart() override {
    if (!connected_) return;
    ++refresh_chart_calls;
  }
  void CancelChart() override { ++cancel_chart_calls; }
  ChartSnapshot SnapshotChart() const override { return chart_; }

  int RequestQuote(const std::string& symbol,
                    ChartInstrument kind = ChartInstrument::kStock) override {
    if (!connected_) return 0;
    ++request_quote_calls;
    last_quote_symbol = symbol;
    last_quote_instrument = kind;
    return ++next_req_id_;
  }
  void CancelQuote() override { ++cancel_quote_calls; }
  QuoteSnapshot SnapshotQuote() const override { return quote_; }

  void RequestServerTime() override {
    if (!connected_) return;
    ++request_server_time_calls;
  }
  InfoSnapshot SnapshotInfo() const override { return info_; }

  void Pump() override { ++pump_calls; }

  std::vector<NewsHeadline> DrainHeadlines() override {
    std::vector<NewsHeadline> out;
    out.swap(headlines_);
    return out;
  }
  std::vector<std::string> DrainStatus() override {
    std::vector<std::string> out;
    out.swap(status_);
    return out;
  }
  std::optional<ArticlePayload> DrainArticle() override {
    auto out = pending_article_;
    pending_article_.reset();
    return out;
  }

 private:
  bool connected_ = false;
  int next_req_id_ = 1000;
  std::vector<NewsHeadline> headlines_;
  std::vector<std::string> status_;
  std::optional<ArticlePayload> pending_article_;
  PortfolioSnapshot portfolio_;
  ChartSnapshot chart_;
  QuoteSnapshot quote_;
  InfoSnapshot info_;
};

}  // namespace dlterm::test

#endif  // DLTERM_TESTS_SUPPORT_FAKE_IBKR_CLIENT_H_
