#ifndef DLTERM_SRC_SERVICES_INFO_SERVICE_H_
#define DLTERM_SRC_SERVICES_INFO_SERVICE_H_

#include <string>
#include <vector>

#include "ibkr_client.h"
#include "services/service_context.h"

class NewsProvider;

namespace dlterm {

// Owns the TWS metadata shown on the splash screen: server version,
// socket connect-time, latest TWS server time, managed accounts, and
// news providers. Driven by the dispatcher under the shared mutex.
class InfoService {
 public:
  explicit InfoService(ServiceContext& ctx) : ctx_(ctx) {}

  // Lifecycle.
  void OnReady();   // nextValidId — fires first reqCurrentTime
  void OnReset();   // disconnect

  // EWrapper hooks (fanned out alongside the existing services).
  void OnManagedAccounts(const std::string& list);
  void OnNewsProviders(const std::vector<NewsProvider>& providers);
  void OnCurrentTime(long long time_sec);

  // Public API. Dispatcher holds the lock.
  void RequestCurrentTime();
  InfoSnapshot Snapshot() const;

 private:
  ServiceContext& ctx_;
  std::vector<std::string> accounts_;
  std::vector<std::pair<std::string, std::string>> providers_;
  std::optional<std::time_t> server_time_;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_SERVICES_INFO_SERVICE_H_
