#include "ibkr_client.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "DefaultEWrapper.h"
#include "EClientSocket.h"
#include "EReader.h"
#include "EReaderOSSignal.h"
#include "NewsProvider.h"
#include "bar.h"
#include "log.h"
#include "redact.h"
#include "services/chart_service.h"
#include "services/info_service.h"
#include "services/news_service.h"
#include "services/portfolio_service.h"
#include "services/quote_service.h"
#include "services/service_context.h"

namespace dlterm {

namespace {

constexpr const char* kCatTws = "tws";
constexpr const char* kCatDispatch = "tws.dispatch";

// Dispatcher: the one EWrapper TWS sees. Owns the shared mutex and
// routes each callback to whichever service tracks the request id.
class Dispatcher : public DefaultEWrapper {
 public:
  void SetClient(EClient* c) { ctx_.client = c; }

  // ---- Public-facing facade (called from main thread via IbkrClient).

  int StartTickerNews(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_DEBUG(kCatTws, "StartTickerNews symbol={}", symbol);
    news_.StartTickerSession(symbol);
    return 1;
  }
  void StartGeneralNews() {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_DEBUG(kCatTws, "StartGeneralNews");
    news_.StartGeneralSession();
  }
  void CancelNews() {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_DEBUG(kCatTws, "CancelNews");
    news_.CancelActive();
  }
  int RequestArticle(const std::string& provider,
                      const std::string& article_id) {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_DEBUG(kCatTws, "RequestArticle provider={} article_id={}",
                      provider, article_id);
    return news_.RequestArticle(provider, article_id);
  }
  std::vector<NewsHeadline> DrainHeadlines() {
    std::lock_guard<std::mutex> lock(mu_);
    return news_.DrainHeadlines();
  }
  std::optional<ArticlePayload> DrainArticle() {
    std::lock_guard<std::mutex> lock(mu_);
    return news_.DrainArticle();
  }
  void EnsureProvidersRequested() {
    std::lock_guard<std::mutex> lock(mu_);
    news_.EnsureProvidersRequested();
  }

  void RequestChart(const std::string& symbol, ChartInstrument kind) {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_DEBUG(kCatTws, "RequestChart symbol={} kind={}", symbol,
                      static_cast<int>(kind));
    chart_.Request(symbol, kind);
  }
  void RefreshChart() {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatTws, "RefreshChart");
    chart_.Refresh();
  }
  void CancelChart() {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_DEBUG(kCatTws, "CancelChart");
    chart_.Cancel();
  }
  ChartSnapshot SnapshotChart() const {
    std::lock_guard<std::mutex> lock(mu_);
    return chart_.Snapshot();
  }

  void RequestQuote(const std::string& symbol, ChartInstrument kind) {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_DEBUG(kCatTws, "RequestQuote symbol={} kind={}", symbol,
                      static_cast<int>(kind));
    quote_.Request(symbol, kind);
  }
  void CancelQuote() {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_DEBUG(kCatTws, "CancelQuote");
    quote_.Cancel();
  }
  QuoteSnapshot SnapshotQuote() const {
    std::lock_guard<std::mutex> lock(mu_);
    return quote_.Snapshot();
  }

  void SubscribePortfolio() {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_DEBUG(kCatTws, "SubscribePortfolio");
    portfolio_.Subscribe();
  }
  void UnsubscribePortfolio() {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_DEBUG(kCatTws, "UnsubscribePortfolio");
    portfolio_.Unsubscribe();
  }
  PortfolioSnapshot SnapshotPortfolio() const {
    std::lock_guard<std::mutex> lock(mu_);
    return portfolio_.Snapshot();
  }

  void RequestServerTime() {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatTws, "RequestServerTime");
    info_.RequestCurrentTime();
  }
  InfoSnapshot SnapshotInfo() const {
    std::lock_guard<std::mutex> lock(mu_);
    return info_.Snapshot();
  }

  std::vector<std::string> DrainStatus() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<std::string> out;
    out.swap(ctx_.status_queue);
    return out;
  }

  void OnDisconnect() {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_INFO(kCatTws, "OnDisconnect: resetting all services");
    news_.OnReset();
    portfolio_.OnReset();
    chart_.OnReset();
    quote_.OnReset();
    info_.OnReset();
  }

  // ---- EWrapper overrides (reader thread).

  void nextValidId(int orderId) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch, "nextValidId orderId={}", orderId);
    news_.OnReady();
    portfolio_.OnReady();
    chart_.OnReady();
    quote_.OnReady();
    info_.OnReady();
  }

  void managedAccounts(const std::string& accountsList) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch, "managedAccounts list={}",
                      MaybeRedactAccountList(accountsList));
    portfolio_.OnManagedAccounts(accountsList);
    info_.OnManagedAccounts(accountsList);
  }

  void currentTime(long long time) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch, "currentTime time={}", time);
    info_.OnCurrentTime(time);
  }

  void connectionClosed() override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_WARN(kCatDispatch, "connectionClosed");
    ctx_.EmitStatus("Connection closed by TWS");
  }

  void error(int id, time_t /*errorTime*/, int errorCode,
             const std::string& errorString,
             const std::string& /*advancedOrderRejectJson*/) override {
    // Skip noisy informational connection-status codes.
    if (errorCode == 2104 || errorCode == 2106 || errorCode == 2107 ||
        errorCode == 2150 || errorCode == 2158 || errorCode == 10197) {
      DLTERM_LOG_TRACE(kCatDispatch, "error (filtered) id={} code={} msg={}",
                        id, errorCode, errorString);
      return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_DEBUG(kCatDispatch, "error id={} code={} msg={}", id, errorCode,
                      errorString);
    if (news_.OnError(id, errorCode)) return;  // silenced (e.g. broadtape probe)
    ctx_.EmitStatus("ERR " + std::to_string(errorCode) + ": " + errorString);
  }

  // News.
  void contractDetails(int reqId, const ContractDetails& details) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch, "contractDetails reqId={}", reqId);
    news_.OnContractDetails(reqId, details);
  }
  void contractDetailsEnd(int reqId) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch, "contractDetailsEnd reqId={}", reqId);
    news_.OnContractDetailsEnd(reqId);
  }
  void historicalNews(int reqId, const std::string& time,
                       const std::string& providerCode,
                       const std::string& articleId,
                       const std::string& headline) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch,
                      "historicalNews reqId={} provider={} id={} time={}",
                      reqId, providerCode, articleId, time);
    news_.OnHistoricalNews(reqId, time, providerCode, articleId, headline);
  }
  void historicalNewsEnd(int reqId, bool /*hasMore*/) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch, "historicalNewsEnd reqId={}", reqId);
    news_.OnHistoricalNewsEnd(reqId);
  }
  void tickNews(int reqId, time_t timeStamp, const std::string& providerCode,
                 const std::string& articleId, const std::string& headline,
                 const std::string& /*extraData*/) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch,
                      "tickNews reqId={} provider={} id={} ts={}",
                      reqId, providerCode, articleId, timeStamp);
    news_.OnTickNews(reqId, timeStamp, providerCode, articleId, headline);
  }
  void newsProviders(const std::vector<NewsProvider>& providers) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch, "newsProviders n={}", providers.size());
    news_.OnNewsProviders(providers);
    info_.OnNewsProviders(providers);
  }
  void newsArticle(int requestId, int articleType,
                    const std::string& articleText) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch,
                      "newsArticle reqId={} type={} bytes={}", requestId,
                      articleType, articleText.size());
    news_.OnNewsArticle(requestId, articleType, articleText);
  }

  // Portfolio.
  void accountUpdateMulti(int reqId, const std::string& account,
                           const std::string& /*modelCode*/,
                           const std::string& key, const std::string& value,
                           const std::string& /*currency*/) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch,
                      "accountUpdateMulti reqId={} acct={} key={} val={}",
                      reqId, MaybeRedactAccount(account), key, value);
    portfolio_.OnAccountUpdateMulti(reqId, account, key, value);
  }
  void accountUpdateMultiEnd(int reqId) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch, "accountUpdateMultiEnd reqId={}", reqId);
    portfolio_.OnAccountUpdateMultiEnd(reqId);
  }
  void positionMulti(int reqId, const std::string& account,
                      const std::string& /*modelCode*/,
                      const Contract& contract, Decimal pos,
                      double avgCost) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch,
                      "positionMulti reqId={} acct={} symbol={} avg={}",
                      reqId, MaybeRedactAccount(account), contract.symbol,
                      avgCost);
    portfolio_.OnPositionMulti(reqId, account, contract, pos, avgCost);
  }
  void positionMultiEnd(int reqId) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch, "positionMultiEnd reqId={}", reqId);
    portfolio_.OnPositionMultiEnd(reqId);
  }
  void pnl(int reqId, double dailyPnL, double unrealizedPnL,
            double realizedPnL) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch,
                      "pnl reqId={} daily={} unreal={} real={}", reqId,
                      dailyPnL, unrealizedPnL, realizedPnL);
    portfolio_.OnPnL(reqId, dailyPnL, unrealizedPnL, realizedPnL);
  }
  void pnlSingle(int reqId, Decimal pos, double dailyPnL, double unrealizedPnL,
                  double realizedPnL, double value) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch,
                      "pnlSingle reqId={} daily={} unreal={} real={} val={}",
                      reqId, dailyPnL, unrealizedPnL, realizedPnL, value);
    portfolio_.OnPnLSingle(reqId, pos, dailyPnL, unrealizedPnL, realizedPnL,
                            value);
  }

  // Quote (reqMktData ticks).
  void tickPrice(int reqId, TickType field, double price,
                  const TickAttrib& /*attribs*/) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch, "tickPrice reqId={} field={} price={}",
                      reqId, static_cast<int>(field), price);
    quote_.OnTickPrice(reqId, static_cast<int>(field), price);
  }
  void tickSize(int reqId, TickType field, Decimal size) override {
    std::lock_guard<std::mutex> lock(mu_);
    const long long size_ll =
        static_cast<long long>(DecimalFunctions::decimalToDouble(size));
    DLTERM_LOG_TRACE(kCatDispatch, "tickSize reqId={} field={} size={}", reqId,
                      static_cast<int>(field), size_ll);
    quote_.OnTickSize(reqId, static_cast<int>(field), size_ll);
  }
  void tickString(int reqId, TickType field,
                   const std::string& value) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch, "tickString reqId={} field={} val={}",
                      reqId, static_cast<int>(field), value);
    quote_.OnTickString(reqId, static_cast<int>(field), value);
  }

  // Chart.
  void historicalData(int reqId, const Bar& bar) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch,
                      "historicalData reqId={} time={} close={}", reqId,
                      bar.time, bar.close);
    chart_.OnHistoricalData(reqId, bar);
  }
  void historicalDataEnd(int reqId, const std::string& /*start*/,
                          const std::string& /*end*/) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch, "historicalDataEnd reqId={}", reqId);
    chart_.OnHistoricalDataEnd(reqId);
  }
  void historicalDataUpdate(int reqId, const Bar& bar) override {
    std::lock_guard<std::mutex> lock(mu_);
    DLTERM_LOG_TRACE(kCatDispatch,
                      "historicalDataUpdate reqId={} time={} close={}",
                      reqId, bar.time, bar.close);
    chart_.OnHistoricalDataUpdate(reqId, bar);
  }

 private:
  mutable std::mutex mu_;
  ServiceContext ctx_;
  NewsService news_{ctx_};
  PortfolioService portfolio_{ctx_};
  ChartService chart_{ctx_};
  QuoteService quote_{ctx_};
  InfoService info_{ctx_};
};

}  // namespace

struct IbkrClient::Impl {
  Impl() : signal(2000), client(&dispatcher, &signal) {
    dispatcher.SetClient(&client);
  }

  Dispatcher dispatcher;
  EReaderOSSignal signal;
  EClientSocket client;
  std::unique_ptr<EReader> reader;
  std::atomic<bool> connected{false};
};

IbkrClient::IbkrClient() : impl_(std::make_unique<Impl>()) {}

IbkrClient::~IbkrClient() { Disconnect(); }

bool IbkrClient::Connect(const std::string& host, int port, int client_id) {
  if (impl_->connected.load()) return true;
  DLTERM_LOG_INFO(kCatTws, "Connect host={} port={} client_id={}", host, port,
                   client_id);
  const bool ok =
      impl_->client.eConnect(host.c_str(), port, client_id, /*extraAuth=*/false);
  if (!ok) {
    DLTERM_LOG_ERROR(kCatTws, "eConnect returned false");
    return false;
  }
  impl_->reader = std::make_unique<EReader>(&impl_->client, &impl_->signal);
  impl_->reader->start();
  impl_->connected.store(true);
  impl_->dispatcher.EnsureProvidersRequested();
  return true;
}

void IbkrClient::Disconnect() {
  if (!impl_->connected.load()) return;
  DLTERM_LOG_INFO(kCatTws, "Disconnect");
  impl_->dispatcher.CancelNews();
  impl_->dispatcher.UnsubscribePortfolio();
  impl_->dispatcher.CancelChart();
  impl_->dispatcher.CancelQuote();
  impl_->client.eDisconnect();
  if (impl_->reader) {
    impl_->reader->stop();
    impl_->reader.reset();
  }
  impl_->connected.store(false);
  impl_->dispatcher.OnDisconnect();
}

bool IbkrClient::IsConnected() const {
  return impl_->connected.load() && impl_->client.isConnected();
}

int IbkrClient::SubscribeNews(const std::string& symbol) {
  if (!IsConnected()) return 0;
  return impl_->dispatcher.StartTickerNews(symbol);
}

void IbkrClient::SubscribeGeneralNews() {
  if (!IsConnected()) return;
  impl_->dispatcher.StartGeneralNews();
}

void IbkrClient::CancelNews() {
  if (!IsConnected()) return;
  impl_->dispatcher.CancelNews();
}

int IbkrClient::RequestArticle(const std::string& provider,
                                const std::string& article_id) {
  if (!IsConnected()) return 0;
  return impl_->dispatcher.RequestArticle(provider, article_id);
}

void IbkrClient::SubscribePortfolio() {
  if (!IsConnected()) return;
  impl_->dispatcher.SubscribePortfolio();
}

void IbkrClient::UnsubscribePortfolio() {
  if (!IsConnected()) return;
  impl_->dispatcher.UnsubscribePortfolio();
}

PortfolioSnapshot IbkrClient::SnapshotPortfolio() const {
  return impl_->dispatcher.SnapshotPortfolio();
}

int IbkrClient::RequestChart(const std::string& symbol,
                              ChartInstrument kind) {
  if (!IsConnected()) return 0;
  impl_->dispatcher.RequestChart(symbol, kind);
  return 1;
}

void IbkrClient::RefreshChart() {
  if (!IsConnected()) return;
  impl_->dispatcher.RefreshChart();
}

void IbkrClient::CancelChart() {
  if (!IsConnected()) return;
  impl_->dispatcher.CancelChart();
}

ChartSnapshot IbkrClient::SnapshotChart() const {
  return impl_->dispatcher.SnapshotChart();
}

int IbkrClient::RequestQuote(const std::string& symbol, ChartInstrument kind) {
  if (!IsConnected()) return 0;
  impl_->dispatcher.RequestQuote(symbol, kind);
  return 1;
}

void IbkrClient::CancelQuote() {
  if (!IsConnected()) return;
  impl_->dispatcher.CancelQuote();
}

QuoteSnapshot IbkrClient::SnapshotQuote() const {
  return impl_->dispatcher.SnapshotQuote();
}

void IbkrClient::RequestServerTime() {
  if (!IsConnected()) return;
  impl_->dispatcher.RequestServerTime();
}

InfoSnapshot IbkrClient::SnapshotInfo() const {
  return impl_->dispatcher.SnapshotInfo();
}

void IbkrClient::Pump() {
  if (!impl_->connected.load() || !impl_->reader) return;
  impl_->reader->processMsgs();
}

std::vector<NewsHeadline> IbkrClient::DrainHeadlines() {
  return impl_->dispatcher.DrainHeadlines();
}

std::vector<std::string> IbkrClient::DrainStatus() {
  return impl_->dispatcher.DrainStatus();
}

std::optional<ArticlePayload> IbkrClient::DrainArticle() {
  return impl_->dispatcher.DrainArticle();
}

}  // namespace dlterm
