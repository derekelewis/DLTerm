// TWS BroadTape feed smoke test. Connects to a local IB Gateway / TWS,
// discovers every BroadTape news contract the account can subscribe to,
// and prints incoming headlines in the same format as TWS desktop's
// "Significant News: All" view: `HH:MM SOURCE  HEADLINE`.
//
// Usage:
//   tws_smoketest [host] [port] [client_id] [duration_seconds]
// Defaults: 127.0.0.1 4002 11 0   (0 = run until SIGINT)

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <set>
#include <string>
#include <utility>
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

// Strip the {A:..:L:..:K:..:C:..} metadata prefix some providers attach.
std::string CleanHeadline(const std::string& raw) {
  if (!raw.empty() && raw.front() == '{') {
    auto end = raw.find('}');
    if (end != std::string::npos) {
      std::string rest = raw.substr(end + 1);
      if (!rest.empty() && rest.front() == '!') rest.erase(0, 1);
      while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
      return rest;
    }
  }
  return raw;
}

std::string FormatLocalHHMM(std::time_t ts) {
  if (ts <= 0) return "--:--";
  std::tm tm{};
  localtime_r(&ts, &tm);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
  return std::string(buf);
}

class FeedWrapper : public DefaultEWrapper {
 public:
  EClient* client = nullptr;
  std::string provider_codes;
  std::set<int> probe_ids;
  std::set<int> stream_ids;
  std::vector<Contract> live_contracts;
  bool providers_received = false;
  bool probes_done = false;

  void error(int id, time_t /*errorTime*/, int errorCode,
             const std::string& errorString,
             const std::string& /*advancedOrderRejectJson*/) override {
    if (errorCode == 2104 || errorCode == 2106 || errorCode == 2107 ||
        errorCode == 2158 || errorCode == 10197) {
      return;  // benign farm/connection-status / dual-session warnings
    }
    if (errorCode == 200 && probe_ids.contains(id)) {
      probe_ids.erase(id);
      if (probe_ids.empty()) probes_done = true;
      return;  // BroadTape probe failure → silent
    }
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
  }

  void contractDetails(int reqId, const ContractDetails& details) override {
    if (!probe_ids.contains(reqId)) return;
    Contract c = details.contract;
    auto colon = c.symbol.find(':');
    if (colon != std::string::npos) c.exchange = c.symbol.substr(0, colon);
    live_contracts.push_back(std::move(c));
  }

  void contractDetailsEnd(int reqId) override {
    if (!probe_ids.contains(reqId)) return;
    probe_ids.erase(reqId);
    if (probe_ids.empty()) probes_done = true;
  }

  void tickNews(int reqId, time_t timeStamp, const std::string& providerCode,
                const std::string& /*articleId*/, const std::string& headline,
                const std::string& /*extraData*/) override {
    if (!stream_ids.contains(reqId)) return;
    const std::time_t ts = static_cast<std::time_t>(timeStamp / 1000);
    std::printf("%s %-7s %s\n", FormatLocalHHMM(ts).c_str(),
                providerCode.c_str(), CleanHeadline(headline).c_str());
    std::fflush(stdout);
  }
};

std::vector<std::string> SplitPlus(const std::string& s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == '+') {
      if (i > start) out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  const char* host = argc > 1 ? argv[1] : "127.0.0.1";
  const int port = argc > 2 ? std::atoi(argv[2]) : 4002;
  const int client_id = argc > 3 ? std::atoi(argv[3]) : 11;
  const int duration_s = argc > 4 ? std::atoi(argv[4]) : 0;

  std::signal(SIGINT, HandleSig);
  std::signal(SIGTERM, HandleSig);

  std::fprintf(stderr, "Connecting to %s:%d as client_id=%d ...\n", host, port,
               client_id);

  FeedWrapper wrapper;
  EReaderOSSignal signal(500);
  EClientSocket client(&wrapper, &signal);

  if (!client.eConnect(host, port, client_id, /*extraAuth=*/false)) {
    std::fprintf(stderr, "eConnect failed\n");
    return 1;
  }

  auto reader = std::make_unique<EReader>(&client, &signal);
  reader->start();
  wrapper.client = &client;

  const auto pump_for = [&](int ms) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (g_running.load() && std::chrono::steady_clock::now() < deadline) {
      signal.waitForSignal();
      reader->processMsgs();
    }
  };
  const auto pump_until = [&](auto cond, int max_ms) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(max_ms);
    while (g_running.load() && !cond() &&
           std::chrono::steady_clock::now() < deadline) {
      signal.waitForSignal();
      reader->processMsgs();
    }
  };

  pump_for(1200);  // drain handshake

  // The gateway told us (via earlier 321 error) that the valid BroadTape
  // source codes are exactly this set. Note these are *source* codes
  // for `secType=NEWS` contracts, distinct from the per-account
  // subscription codes returned by reqNewsProviders (which include
  // DJ-N, DJ-RT, DJ-RTA, etc.). Probes that aren't subscribed return
  // error code 200 and are silently skipped.
  static const std::vector<std::string> kBroadtapeSources = {
      "BRFG", "BRFUPDN", "DJ",  "DJNL", "BZ",
      "DJTOP", "FLY",   "BT", "MT",
  };

  std::fprintf(stderr, "Probing BroadTape sources:");
  for (const auto& s : kBroadtapeSources) std::fprintf(stderr, " %s", s.c_str());
  std::fprintf(stderr, "\n");

  int next_id = 7100;
  for (const std::string& src : kBroadtapeSources) {
    Contract bt;
    bt.symbol = src + ":" + src + "_ALL";
    bt.secType = "NEWS";
    bt.exchange = src;
    wrapper.probe_ids.insert(next_id);
    client.reqContractDetails(next_id++, bt);
  }
  pump_until([&] { return wrapper.probes_done; }, 5000);

  if (wrapper.live_contracts.empty()) {
    std::fprintf(stderr, "No BroadTape contracts available for this account.\n");
    client.eDisconnect();
    reader->stop();
    return 2;
  }

  std::fprintf(stderr, "Subscribing to %zu BroadTape feed(s):",
               wrapper.live_contracts.size());
  for (const auto& c : wrapper.live_contracts) {
    std::fprintf(stderr, " %s", c.symbol.c_str());
  }
  std::fprintf(stderr, "\n\nTIME  SOURCE  HEADLINE\n");

  for (const auto& c : wrapper.live_contracts) {
    const int sid = next_id++;
    wrapper.stream_ids.insert(sid);
    client.reqMktData(sid, c, "mdoff,292", false, false, TagValueListSPtr());
  }

  // Also exercise ticker-specific live news per the docs:
  // reqMktData(reqId, STK_CONTRACT, "mdoff,292:<PROVIDER>+<PROVIDER>...").
  {
    Contract stk;
    stk.symbol = "NVDA";
    stk.secType = "STK";
    stk.exchange = "SMART";
    stk.currency = "USD";
    const std::string ticks =
        "mdoff,292:BRFG+BRFUPDN+DJ-N+DJ-RT+DJ-RTA+DJ-RTE+DJ-RTG+DJNL";
    const int sid = next_id++;
    wrapper.stream_ids.insert(sid);
    std::fprintf(stderr, "Per-ticker live news: NVDA  ticks=%s\n",
                 ticks.c_str());
    client.reqMktData(sid, stk, ticks, false, false, TagValueListSPtr());
  }

  // Stream until SIGINT or duration elapses.
  const auto t0 = std::chrono::steady_clock::now();
  while (g_running.load()) {
    if (duration_s > 0) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - t0)
                               .count();
      if (elapsed >= duration_s) break;
    }
    signal.waitForSignal();
    reader->processMsgs();
  }

  std::fprintf(stderr, "\nShutting down.\n");
  for (int sid : wrapper.stream_ids) client.cancelMktData(sid);
  client.eDisconnect();
  reader->stop();
  return 0;
}
