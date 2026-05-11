// Standalone futures smoketest. Mirrors what ChartService does for
// the futures chart paths: connects to TWS / IB Gateway and issues
// reqHistoricalData. Two modes:
//   contfut_smoketest [host] [port] [client_id] [duration_s] [local]
//     local is optional. With it: chart that specific FUT localSymbol
//     (e.g. CLM6) on NYMEX with keepUpToDate=true. Without it: chart
//     CL CONTFUT with keepUpToDate=false.
// Defaults: 127.0.0.1 4002 12 8

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

#include "Contract.h"
#include "Decimal.h"
#include "DefaultEWrapper.h"
#include "EClientSocket.h"
#include "EReader.h"
#include "EReaderOSSignal.h"
#include "bar.h"

namespace {

std::atomic<bool> g_running{true};
void HandleSig(int /*sig*/) { g_running.store(false); }

class ChartWrapper : public DefaultEWrapper {
 public:
  int req_id = 0;
  int bar_count = 0;
  double last_close = 0.0;
  std::time_t last_ts = 0;
  bool end_received = false;

  void error(int id, time_t /*errorTime*/, int errorCode,
             const std::string& errorString,
             const std::string& /*advancedOrderRejectJson*/) override {
    if (errorCode == 2104 || errorCode == 2106 || errorCode == 2107 ||
        errorCode == 2158 || errorCode == 10197) return;
    std::fprintf(stderr, "[error] id=%d code=%d  %s\n", id, errorCode,
                 errorString.c_str());
  }

  void nextValidId(int /*orderId*/) override {}

  void historicalData(int rid, const Bar& bar) override {
    if (rid != req_id) return;
    ++bar_count;
    last_close = bar.close;
    last_ts = static_cast<std::time_t>(std::stoll(bar.time));
    std::tm tm{};
    localtime_r(&last_ts, &tm);
    std::printf("  %04d-%02d-%02d %02d:%02d  O=%.2f H=%.2f L=%.2f C=%.2f\n",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, bar.open, bar.high, bar.low, bar.close);
    std::fflush(stdout);
  }

  void historicalDataEnd(int rid, const std::string& /*s*/,
                         const std::string& /*e*/) override {
    if (rid != req_id) return;
    end_received = true;
  }
};

}  // namespace

int main(int argc, char** argv) {
  const char* host = argc > 1 ? argv[1] : "127.0.0.1";
  const int port = argc > 2 ? std::atoi(argv[2]) : 4002;
  const int client_id = argc > 3 ? std::atoi(argv[3]) : 12;
  const int duration_s = argc > 4 ? std::atoi(argv[4]) : 8;
  const char* local_sym = argc > 5 ? argv[5] : nullptr;  // e.g. "CLM6"

  std::signal(SIGINT, HandleSig);
  std::signal(SIGTERM, HandleSig);

  std::fprintf(stderr, "Connecting to %s:%d as client_id=%d ...\n", host, port,
               client_id);

  ChartWrapper w;
  EReaderOSSignal signal(500);
  EClientSocket client(&w, &signal);

  if (!client.eConnect(host, port, client_id, /*extraAuth=*/false)) {
    std::fprintf(stderr, "eConnect failed (is IB Gateway up on %s:%d?)\n",
                 host, port);
    return 1;
  }
  auto reader = std::make_unique<EReader>(&client, &signal);
  reader->start();

  const auto pump_ms = [&](int ms) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (g_running.load() && std::chrono::steady_clock::now() < deadline) {
      signal.waitForSignal();
      reader->processMsgs();
    }
  };
  pump_ms(1500);  // handshake

  Contract c;
  bool keep_up_to_date = false;
  if (local_sym) {
    c.localSymbol = local_sym;
    c.secType = "FUT";
    c.exchange = "NYMEX";
    c.currency = "USD";
    keep_up_to_date = true;
    std::fprintf(stderr,
                  "Requesting reqHistoricalData(localSymbol=%s/FUT/NYMEX, 1 D, "
                  "5 mins, TRADES, useRTH=0, formatDate=2, "
                  "keepUpToDate=true)\n",
                  local_sym);
  } else {
    c.symbol = "CL";
    c.secType = "CONTFUT";
    c.exchange = "NYMEX";
    c.currency = "USD";
    std::fprintf(stderr,
                  "Requesting reqHistoricalData(CL/CONTFUT/NYMEX, 1 D, "
                  "5 mins, TRADES, useRTH=0, formatDate=2, "
                  "keepUpToDate=false)\n");
  }

  w.req_id = 9100;
  client.reqHistoricalData(w.req_id, c, "", "1 D", "5 mins", "TRADES", 0, 2,
                           keep_up_to_date, TagValueListSPtr());

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(duration_s);
  while (g_running.load() && !w.end_received &&
         std::chrono::steady_clock::now() < deadline) {
    signal.waitForSignal();
    reader->processMsgs();
  }

  if (w.end_received) {
    std::tm tm{};
    localtime_r(&w.last_ts, &tm);
    std::fprintf(stderr,
                  "\nDone. %d bars received. Last bar %04d-%02d-%02d %02d:%02d "
                  "close=%.2f\n",
                  w.bar_count, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, w.last_close);
  } else {
    std::fprintf(stderr,
                  "\nTimed out after %ds with %d bars (no historicalDataEnd).\n",
                  duration_s, w.bar_count);
  }

  client.cancelHistoricalData(w.req_id);
  client.eDisconnect();
  reader->stop();
  return w.end_received ? 0 : 2;
}
