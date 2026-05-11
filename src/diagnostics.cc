#include "diagnostics.h"

#include <array>
#include <cstddef>
#include <mutex>

namespace dlterm::diag {
namespace {

constexpr std::size_t kFrameWindow = 60;

struct State {
  std::mutex mu;
  std::array<double, kFrameWindow> recent{};
  std::size_t head = 0;
  std::size_t count = 0;
  std::size_t total = 0;
  std::string current_screen;
  bool tws_connected = false;
  std::string tws_host;
  int tws_port = 0;
  int tws_client_id = 0;
};

State& Global() {
  static State s;
  return s;
}

}  // namespace

void RecordFrame(double ms) {
  auto& s = Global();
  std::lock_guard lock(s.mu);
  s.recent[s.head] = ms;
  s.head = (s.head + 1) % kFrameWindow;
  if (s.count < kFrameWindow) ++s.count;
  ++s.total;
}

void SetCurrentScreen(std::string name) {
  auto& s = Global();
  std::lock_guard lock(s.mu);
  s.current_screen = std::move(name);
}

void SetTwsConnected(bool v) {
  auto& s = Global();
  std::lock_guard lock(s.mu);
  s.tws_connected = v;
}

void SetTwsTarget(std::string host, int port, int client_id) {
  auto& s = Global();
  std::lock_guard lock(s.mu);
  s.tws_host = std::move(host);
  s.tws_port = port;
  s.tws_client_id = client_id;
}

Snapshot Get() {
  auto& s = Global();
  std::lock_guard lock(s.mu);
  Snapshot out;
  out.frame_count = s.total;
  if (s.count > 0) {
    double sum = 0.0;
    for (std::size_t i = 0; i < s.count; ++i) sum += s.recent[i];
    out.frame_avg_ms = sum / static_cast<double>(s.count);
    const std::size_t last_idx =
        (s.head + kFrameWindow - 1) % kFrameWindow;
    out.frame_last_ms = s.recent[last_idx];
  }
  out.current_screen = s.current_screen;
  out.tws_connected = s.tws_connected;
  out.tws_host = s.tws_host;
  out.tws_port = s.tws_port;
  out.tws_client_id = s.tws_client_id;
  return out;
}

void ResetForTesting() {
  auto& s = Global();
  std::lock_guard lock(s.mu);
  s.recent.fill(0.0);
  s.head = 0;
  s.count = 0;
  s.total = 0;
  s.current_screen.clear();
  s.tws_connected = false;
  s.tws_host.clear();
  s.tws_port = 0;
  s.tws_client_id = 0;
}

}  // namespace dlterm::diag
