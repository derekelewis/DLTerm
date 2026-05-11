#ifndef DLTERM_SRC_SERVICES_NEWS_SERVICE_H_
#define DLTERM_SRC_SERVICES_NEWS_SERVICE_H_

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Contract.h"
#include "ibkr_client.h"
#include "services/service_context.h"

class NewsProvider;
struct ContractDetails;

namespace dlterm {

// Per-ticker NEWS (historical + live tail), BroadTape discovery and
// streaming, and full-article fetch. Called by the dispatcher under
// the shared mutex.
class NewsService {
 public:
  explicit NewsService(ServiceContext& ctx) : ctx_(ctx) {}

  // Public API (dispatcher holds the lock).
  void StartTickerSession(const std::string& symbol);
  void StartGeneralSession();
  void CancelActive();
  int RequestArticle(const std::string& provider,
                      const std::string& article_id);

  // Returns/clears state. Called externally — dispatcher takes the
  // lock around each.
  std::vector<NewsHeadline> DrainHeadlines();
  std::optional<ArticlePayload> DrainArticle();

  // Lifecycle.
  void OnReady();   // nextValidId
  void OnReset();   // disconnect
  void EnsureProvidersRequested();  // sets a flag; sent on OnReady

  // Returns true if this service owns the given request id (used by
  // the dispatcher's `error()` to silence broadtape probe failures).
  bool OwnsReqId(int req_id) const;
  // Returns true if the given req id is an active broadtape probe.
  bool IsBroadtapeProbe(int req_id) const {
    return broadtape_probe_ids_.contains(req_id);
  }

  // EWrapper hooks.
  void OnContractDetails(int req_id, const ContractDetails& details);
  void OnContractDetailsEnd(int req_id);
  void OnHistoricalNews(int req_id, const std::string& time_str,
                         const std::string& provider,
                         const std::string& article_id,
                         const std::string& headline);
  void OnHistoricalNewsEnd(int req_id);
  void OnTickNews(int req_id, time_t ts, const std::string& provider,
                   const std::string& article_id, const std::string& headline);
  void OnNewsProviders(const std::vector<NewsProvider>& providers);
  void OnNewsArticle(int req_id, int article_type, const std::string& text);
  // Returns true if the error was consumed (silent broadtape probe fail).
  bool OnError(int req_id, int code);

 private:
  enum class Stage {
    kIdle,
    kResolving,
    kFetchingHist,
    kStreamingTicker,
    kBroadtapeDiscover,
    kBroadtapeStream,
  };

  static Contract MakeStockContract(const std::string& symbol);
  void HandleBroadtapeProbeFinished(int req_id);

  ServiceContext& ctx_;

  std::vector<NewsHeadline> headlines_;
  std::optional<ArticlePayload> article_;
  std::set<std::string> seen_article_ids_;

  std::string provider_codes_;
  bool providers_requested_ = false;
  bool want_providers_ = false;

  Stage stage_ = Stage::kIdle;
  std::string symbol_;
  int con_id_ = 0;
  int details_req_id_ = 0;
  int hist_req_id_ = 0;
  int ticker_stream_req_id_ = 0;
  int hist_count_ = 0;

  std::set<int> broadtape_probe_ids_;
  std::vector<Contract> broadtape_contracts_;
  std::set<int> broadtape_stream_req_ids_;
  std::vector<std::pair<int, Contract>> pending_broadtape_probes_;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_SERVICES_NEWS_SERVICE_H_
