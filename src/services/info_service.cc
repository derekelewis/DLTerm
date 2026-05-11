#include "services/info_service.h"

#include <cctype>
#include <string>

#include "EClient.h"
#include "EDecoder.h"
#include "NewsProvider.h"

namespace dlterm {

namespace {

std::vector<std::string> SplitCsv(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ',') {
      if (!cur.empty()) out.push_back(std::move(cur));
      cur.clear();
    } else if (!std::isspace(static_cast<unsigned char>(c))) {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(std::move(cur));
  return out;
}

}  // namespace

void InfoService::OnReady() {
  // Refresh the server-time clock as soon as TWS hands us nextValidId.
  RequestCurrentTime();
}

void InfoService::OnReset() {
  accounts_.clear();
  providers_.clear();
  server_time_.reset();
}

void InfoService::OnManagedAccounts(const std::string& list) {
  accounts_ = SplitCsv(list);
}

void InfoService::OnNewsProviders(const std::vector<NewsProvider>& providers) {
  providers_.clear();
  providers_.reserve(providers.size());
  for (const auto& p : providers) {
    providers_.emplace_back(p.providerCode, p.providerName);
  }
}

void InfoService::OnCurrentTime(long long time_sec) {
  server_time_ = static_cast<std::time_t>(time_sec);
}

void InfoService::RequestCurrentTime() {
  if (ctx_.client) ctx_.client->reqCurrentTime();
}

InfoSnapshot InfoService::Snapshot() const {
  InfoSnapshot s;
  if (ctx_.client) {
    s.server_version = ctx_.client->serverVersion();
    s.tws_connect_time = ctx_.client->TwsConnectionTime();
  }
  s.client_min_version = MIN_CLIENT_VER;
  s.client_max_version = MAX_CLIENT_VER;
#ifdef DLTERM_TWS_API_VERSION
  s.client_api_version = DLTERM_TWS_API_VERSION;
#endif
  s.server_time = server_time_;
  s.accounts = accounts_;
  s.news_providers = providers_;
  return s;
}

}  // namespace dlterm
