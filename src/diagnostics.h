#ifndef DLTERM_SRC_DIAGNOSTICS_H_
#define DLTERM_SRC_DIAGNOSTICS_H_

#include <cstddef>
#include <string>

namespace dlterm::diag {

// A snapshot of system state for the DEBUG overlay screen and any
// other diagnostics consumers. All fields are read-only views; mutate
// via the Set* / Record* free functions.
struct Snapshot {
  double frame_avg_ms = 0.0;
  double frame_last_ms = 0.0;
  std::size_t frame_count = 0;
  std::string current_screen;
  bool tws_connected = false;
  std::string tws_host;
  int tws_port = 0;
  int tws_client_id = 0;
};

void RecordFrame(double ms);
void SetCurrentScreen(std::string s);
void SetTwsConnected(bool v);
void SetTwsTarget(std::string host, int port, int client_id);

Snapshot Get();
void ResetForTesting();

}  // namespace dlterm::diag

#endif  // DLTERM_SRC_DIAGNOSTICS_H_
