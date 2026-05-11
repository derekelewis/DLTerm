// Empirical probe: does IB return news for futures contracts?
//
// For each of {CL CONTFUT/NYMEX, CLM6 FUT/NYMEX}:
//   1. Resolve the contract via reqContractDetails -> conId.
//   2. reqHistoricalNews(conId, all providers, 50). Print count + samples.
//   3. Subscribe live via reqMktData(..., "mdoff,292:<providers>") for
//      a short window. Print any tickNews that arrive.
//
// Usage:
//   futures_news_smoketest [host] [port] [client_id] [live_window_s]
// Defaults: 127.0.0.1 4002 14 15

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "Contract.h"
#include "DefaultEWrapper.h"
#include "EClientSocket.h"
#include "EReader.h"
#include "EReaderOSSignal.h"
#include "NewsProvider.h"

namespace {

std::atomic<bool> g_running{true};
void HandleSig(int /*sig*/) { g_running.store(false); }

struct ContractTarget {
  std::string label;
  std::string symbol;
  std::string local_symbol;
  std::string sec_type;
  std::string exchange;
  std::string currency = "USD";
};

class Wrapper : public DefaultEWrapper {
 public:
  std::string provider_codes;   // "+"-joined
  bool providers_received = false;

  int waiting_cd_req = 0;
  int last_con_id = 0;
  bool cd_end = false;

  int waiting_hist_req = 0;
  int hist_count = 0;
  std::vector<std::string> sample_headlines;
  bool hist_end = false;

  std::vector<int> live_req_ids;
  int live_tick_count = 0;

  void error(int id, time_t /*errorTime*/, int errorCode,
             const std::string& errorString,
             const std::string& /*advancedOrderRejectJson*/) override {
    if (errorCode == 2104 || errorCode == 2106 || errorCode == 2107 ||
        errorCode == 2158 || errorCode == 10197) return;
    std::fprintf(stderr, "[error] id=%d code=%d  %s\n", id, errorCode,
                 errorString.c_str());
  }

  void nextValidId(int /*orderId*/) override {}

  void newsProviders(const std::vector<NewsProvider>& providers) override {
    std::string joined;
    for (const auto& p : providers) {
      if (!joined.empty()) joined.push_back('+');
      joined.append(p.providerCode);
    }
    provider_codes = std::move(joined);
    providers_received = true;
    std::fprintf(stderr, "Providers: %s\n", provider_codes.c_str());
  }

  void contractDetails(int reqId, const ContractDetails& details) override {
    if (reqId != waiting_cd_req) return;
    last_con_id = details.contract.conId;
  }
  void contractDetailsEnd(int reqId) override {
    if (reqId != waiting_cd_req) return;
    cd_end = true;
  }

  void historicalNews(int reqId, const std::string& time_str,
                       const std::string& providerCode,
                       const std::string& /*articleId*/,
                       const std::string& headline) override {
    if (reqId != waiting_hist_req) return;
    ++hist_count;
    if (sample_headlines.size() < 5) {
      sample_headlines.push_back(time_str + "  " + providerCode + "  " +
                                  headline);
    }
  }
  void historicalNewsEnd(int reqId, bool /*hasMore*/) override {
    if (reqId != waiting_hist_req) return;
    hist_end = true;
  }

  void tickNews(int reqId, time_t timeStamp, const std::string& providerCode,
                 const std::string& /*articleId*/, const std::string& headline,
                 const std::string& /*extraData*/) override {
    bool ours = false;
    for (int id : live_req_ids) if (id == reqId) { ours = true; break; }
    if (!ours) return;
    ++live_tick_count;
    const std::time_t ts = static_cast<std::time_t>(timeStamp / 1000);
    std::tm tm{};
    localtime_r(&ts, &tm);
    std::printf("  [live] %02d:%02d %-7s %s\n", tm.tm_hour, tm.tm_min,
                providerCode.c_str(), headline.c_str());
    std::fflush(stdout);
  }
};

}  // namespace

int main(int argc, char** argv) {
  const char* host = argc > 1 ? argv[1] : "127.0.0.1";
  const int port = argc > 2 ? std::atoi(argv[2]) : 4002;
  const int client_id = argc > 3 ? std::atoi(argv[3]) : 14;
  const int live_window_s = argc > 4 ? std::atoi(argv[4]) : 15;

  std::signal(SIGINT, HandleSig);
  std::signal(SIGTERM, HandleSig);

  std::fprintf(stderr, "Connecting to %s:%d as client_id=%d ...\n", host, port,
               client_id);

  Wrapper w;
  EReaderOSSignal signal(500);
  EClientSocket client(&w, &signal);

  if (!client.eConnect(host, port, client_id, /*extraAuth=*/false)) {
    std::fprintf(stderr, "eConnect failed (is IB Gateway up on %s:%d?)\n",
                 host, port);
    return 1;
  }
  auto reader = std::make_unique<EReader>(&client, &signal);
  reader->start();

  const auto pump_until = [&](auto cond, int max_ms) {
    const auto deadline = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(max_ms);
    while (g_running.load() && !cond() &&
           std::chrono::steady_clock::now() < deadline) {
      signal.waitForSignal();
      reader->processMsgs();
    }
  };

  pump_until([&] { return false; }, 1500);   // drain handshake.

  // Need provider codes to feed reqHistoricalNews.
  client.reqNewsProviders();
  pump_until([&] { return w.providers_received; }, 5000);
  if (!w.providers_received || w.provider_codes.empty()) {
    std::fprintf(stderr, "No news providers — account isn't subscribed to "
                          "any feeds; nothing further to test.\n");
    client.eDisconnect();
    reader->stop();
    return 2;
  }

  const std::vector<ContractTarget> targets = {
      {"CL CONTFUT/NYMEX",  "CL",  "",     "CONTFUT", "NYMEX"},
      {"CLM6 FUT/NYMEX",    "",    "CLM6", "FUT",     "NYMEX"},
  };

  int next_id = 9300;
  std::vector<int> resolved_con_ids;
  std::vector<std::string> resolved_labels;

  for (const auto& t : targets) {
    std::fprintf(stderr, "\n=== %s ===\n", t.label.c_str());
    Contract c;
    if (!t.symbol.empty()) c.symbol = t.symbol;
    if (!t.local_symbol.empty()) c.localSymbol = t.local_symbol;
    c.secType = t.sec_type;
    c.exchange = t.exchange;
    c.currency = t.currency;

    w.last_con_id = 0;
    w.cd_end = false;
    w.waiting_cd_req = next_id++;
    client.reqContractDetails(w.waiting_cd_req, c);
    pump_until([&] { return w.cd_end; }, 5000);
    if (w.last_con_id == 0) {
      std::fprintf(stderr, "  contract resolve failed.\n");
      continue;
    }
    std::fprintf(stderr, "  conId=%d\n", w.last_con_id);

    // Historical: ask for up to 50 over an open window.
    w.hist_count = 0;
    w.sample_headlines.clear();
    w.hist_end = false;
    w.waiting_hist_req = next_id++;
    client.reqHistoricalNews(w.waiting_hist_req, w.last_con_id,
                              w.provider_codes, "", "", 50,
                              TagValueListSPtr());
    pump_until([&] { return w.hist_end; }, 8000);
    std::fprintf(stderr, "  reqHistoricalNews: %d headline(s)\n",
                 w.hist_count);
    for (const auto& h : w.sample_headlines) {
      std::fprintf(stderr, "    %s\n", h.c_str());
    }

    resolved_con_ids.push_back(w.last_con_id);
    resolved_labels.push_back(t.label);
  }

  if (resolved_con_ids.empty()) {
    std::fprintf(stderr, "\nNo contracts resolved; skipping live probe.\n");
    client.eDisconnect();
    reader->stop();
    return 0;
  }

  // Live probe: subscribe tickNews via mdoff,292 on every resolved conId.
  std::fprintf(stderr, "\n=== Live tick-news probe (%ds) ===\n",
                live_window_s);
  const std::string ticks = "mdoff,292:" + w.provider_codes;
  for (std::size_t i = 0; i < resolved_con_ids.size(); ++i) {
    Contract c;
    c.conId = resolved_con_ids[i];
    // exchange/currency required even when conId is set.
    c.exchange = "NYMEX";
    c.currency = "USD";
    if (i == 0) c.secType = "CONTFUT";
    else c.secType = "FUT";
    const int rid = next_id++;
    w.live_req_ids.push_back(rid);
    std::fprintf(stderr, "  subscribed %s reqId=%d\n",
                 resolved_labels[i].c_str(), rid);
    client.reqMktData(rid, c, ticks, false, false, TagValueListSPtr());
  }

  pump_until([&] { return false; }, live_window_s * 1000);
  std::fprintf(stderr, "\nLive ticks received: %d\n", w.live_tick_count);

  for (int rid : w.live_req_ids) client.cancelMktData(rid);
  client.eDisconnect();
  reader->stop();
  return 0;
}
