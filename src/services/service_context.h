#ifndef DLTERM_SRC_SERVICES_SERVICE_CONTEXT_H_
#define DLTERM_SRC_SERVICES_SERVICE_CONTEXT_H_

#include <string>
#include <utility>
#include <vector>

class EClient;

namespace dlterm {

// Shared state that every IbkrClient service needs. All access is
// serialized externally by the dispatcher's mutex — services do not
// lock themselves.
struct ServiceContext {
  EClient* client = nullptr;
  int next_req_id = 1000;
  std::vector<std::string> status_queue;

  int NextReqId() { return ++next_req_id; }
  void EmitStatus(std::string msg) {
    status_queue.push_back(std::move(msg));
  }
};

}  // namespace dlterm

#endif  // DLTERM_SRC_SERVICES_SERVICE_CONTEXT_H_
