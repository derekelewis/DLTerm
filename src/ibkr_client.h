#ifndef DLTERM_SRC_IBKR_CLIENT_H_
#define DLTERM_SRC_IBKR_CLIENT_H_

#include <ctime>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dlterm {

struct NewsHeadline {
  std::time_t ts = 0;
  std::string provider;
  std::string article_id;
  std::string headline;
};

struct ArticlePayload {
  int request_id = 0;
  int article_type = 0;  // 0=plain text, 1=HTML, 2=binary (skipped)
  std::string text;
};

// One row in the rendered portfolio table. Positions for the same
// instrument across multiple accounts are merged here on the way out.
struct PortfolioPosition {
  std::string symbol;
  std::string sec_type;
  std::string currency;
  std::string accounts;  // comma-joined contributing account codes
  double position = 0;
  double avg_cost = 0;
  double market_value = 0;
  double daily_pnl = 0;
  double unrealized_pnl = 0;
  double realized_pnl = 0;
};

struct AccountTotals {
  std::string account;
  std::map<std::string, std::string> values;
};

struct PortfolioSnapshot {
  bool ready = false;
  std::vector<std::string> accounts;
  std::vector<AccountTotals> totals;
  std::vector<PortfolioPosition> positions;
};

struct ChartBar {
  std::time_t ts = 0;
  double open = 0;
  double high = 0;
  double low = 0;
  double close = 0;
  double volume = 0;
};

// What kind of instrument a chart request is for. Drives the Contract
// the service constructs and whether keepUpToDate is allowed.
enum class ChartInstrument {
  kStock,              // STK / SMART / USD; live keepUpToDate=true.
  kContinuousFuture,   // CONTFUT / <exchange> / USD; keepUpToDate=false.
  kFutureLocal,        // FUT by localSymbol / <exchange> / USD;
                       // live keepUpToDate=true on the specific expiry.
};

struct ChartSnapshot {
  std::string symbol;
  ChartInstrument kind = ChartInstrument::kStock;
  bool ready = false;
  std::vector<ChartBar> bars;
};

// One symbol's live tick snapshot, populated by reqMktData callbacks.
// `ready` flips true on the first non-sentinel LAST tick. Price fields
// are NaN until received; size fields are -1. Test with std::isfinite()
// / `>= 0` before display.
struct QuoteSnapshot {
  std::string symbol;
  ChartInstrument kind = ChartInstrument::kStock;
  bool ready = false;

  double bid   = std::numeric_limits<double>::quiet_NaN();
  double ask   = std::numeric_limits<double>::quiet_NaN();
  double last  = std::numeric_limits<double>::quiet_NaN();
  double high  = std::numeric_limits<double>::quiet_NaN();
  double low   = std::numeric_limits<double>::quiet_NaN();
  double open  = std::numeric_limits<double>::quiet_NaN();
  double close = std::numeric_limits<double>::quiet_NaN();  // previous close

  long long bid_size = -1;
  long long ask_size = -1;
  long long last_size = -1;
  long long volume = -1;

  std::time_t last_ts = 0;
};

// Lightweight TWS-side metadata for the splash screen.
struct InfoSnapshot {
  int server_version = 0;
  // Marketing version of the linked TWS client library, parsed from
  // API_VersionNum.txt at CMake configure time (e.g. "10.46.01"). Empty
  // if the file wasn't found.
  std::string client_api_version;
  // Protocol version range supported by the linked TWS client library
  // (compile-time constants MIN_CLIENT_VER..MAX_CLIENT_VER).
  int client_min_version = 0;
  int client_max_version = 0;
  std::string tws_connect_time;          // raw e.g. "20260510 13:22:00 EST"
  std::optional<std::time_t> server_time;  // last reqCurrentTime reply
  std::vector<std::string> accounts;
  // Provider code -> friendly name. Empty when not yet received.
  std::vector<std::pair<std::string, std::string>> news_providers;
};

// Public interface implemented by both the live IbkrClient and test
// fakes. Screens take this rather than the concrete class.
class IIbkrClient {
 public:
  virtual ~IIbkrClient() = default;

  virtual bool Connect(const std::string& host, int port, int client_id) = 0;
  virtual void Disconnect() = 0;
  virtual bool IsConnected() const = 0;

  virtual int SubscribeNews(const std::string& symbol) = 0;
  virtual void SubscribeGeneralNews() = 0;
  virtual void CancelNews() = 0;

  virtual int RequestArticle(const std::string& provider,
                              const std::string& article_id) = 0;

  virtual void SubscribePortfolio() = 0;
  virtual void UnsubscribePortfolio() = 0;
  virtual PortfolioSnapshot SnapshotPortfolio() const = 0;

  virtual int RequestChart(
      const std::string& symbol,
      ChartInstrument kind = ChartInstrument::kStock) = 0;
  // Re-runs the current chart request in place — used to refresh
  // bars for CONTFUT (which TWS won't keep live). No-op if no chart
  // is active. Old bars stay on screen until the new batch lands.
  virtual void RefreshChart() = 0;
  virtual void CancelChart() = 0;
  virtual ChartSnapshot SnapshotChart() const = 0;

  virtual int RequestQuote(
      const std::string& symbol,
      ChartInstrument kind = ChartInstrument::kStock) = 0;
  virtual void CancelQuote() = 0;
  virtual QuoteSnapshot SnapshotQuote() const = 0;

  // Splash-screen TWS metadata. RequestServerTime fires reqCurrentTime;
  // SnapshotInfo() returns the cached snapshot (cheap to call every frame).
  virtual void RequestServerTime() = 0;
  virtual InfoSnapshot SnapshotInfo() const = 0;

  virtual void Pump() = 0;

  virtual std::vector<NewsHeadline> DrainHeadlines() = 0;
  virtual std::vector<std::string> DrainStatus() = 0;
  virtual std::optional<ArticlePayload> DrainArticle() = 0;
};

// Live IbkrClient connected to a real TWS / IB Gateway instance.
class IbkrClient final : public IIbkrClient {
 public:
  IbkrClient();
  ~IbkrClient() override;

  IbkrClient(const IbkrClient&) = delete;
  IbkrClient& operator=(const IbkrClient&) = delete;

  bool Connect(const std::string& host, int port, int client_id) override;
  void Disconnect() override;
  bool IsConnected() const override;

  int SubscribeNews(const std::string& symbol) override;
  void SubscribeGeneralNews() override;
  void CancelNews() override;

  int RequestArticle(const std::string& provider,
                      const std::string& article_id) override;

  void SubscribePortfolio() override;
  void UnsubscribePortfolio() override;
  PortfolioSnapshot SnapshotPortfolio() const override;

  int RequestChart(const std::string& symbol,
                    ChartInstrument kind = ChartInstrument::kStock) override;
  void RefreshChart() override;
  void CancelChart() override;
  ChartSnapshot SnapshotChart() const override;

  int RequestQuote(const std::string& symbol,
                    ChartInstrument kind = ChartInstrument::kStock) override;
  void CancelQuote() override;
  QuoteSnapshot SnapshotQuote() const override;

  void RequestServerTime() override;
  InfoSnapshot SnapshotInfo() const override;

  void Pump() override;

  std::vector<NewsHeadline> DrainHeadlines() override;
  std::vector<std::string> DrainStatus() override;
  std::optional<ArticlePayload> DrainArticle() override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_IBKR_CLIENT_H_
