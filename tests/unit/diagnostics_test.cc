#include "diagnostics.h"

#include <gtest/gtest.h>

namespace dlterm::diag {
namespace {

class DiagnosticsTest : public ::testing::Test {
 protected:
  void SetUp() override { ResetForTesting(); }
  void TearDown() override { ResetForTesting(); }
};

TEST_F(DiagnosticsTest, EmptySnapshot) {
  auto s = Get();
  EXPECT_DOUBLE_EQ(s.frame_avg_ms, 0.0);
  EXPECT_DOUBLE_EQ(s.frame_last_ms, 0.0);
  EXPECT_EQ(s.frame_count, 0u);
  EXPECT_TRUE(s.current_screen.empty());
  EXPECT_FALSE(s.tws_connected);
}

TEST_F(DiagnosticsTest, RecordFrameTracksAvgAndLast) {
  RecordFrame(10.0);
  RecordFrame(20.0);
  RecordFrame(30.0);
  auto s = Get();
  EXPECT_DOUBLE_EQ(s.frame_last_ms, 30.0);
  EXPECT_DOUBLE_EQ(s.frame_avg_ms, 20.0);
  EXPECT_EQ(s.frame_count, 3u);
}

TEST_F(DiagnosticsTest, RingWindowDropsOldFrames) {
  // Push more than the window (60). Newer 60 should dominate.
  for (int i = 0; i < 100; ++i) RecordFrame(16.0);
  auto s = Get();
  EXPECT_DOUBLE_EQ(s.frame_avg_ms, 16.0);
  EXPECT_EQ(s.frame_count, 100u);
}

TEST_F(DiagnosticsTest, SettersStickInSnapshot) {
  SetCurrentScreen("ChartScreen");
  SetTwsTarget("127.0.0.1", 4002, 7);
  SetTwsConnected(true);
  auto s = Get();
  EXPECT_EQ(s.current_screen, "ChartScreen");
  EXPECT_EQ(s.tws_host, "127.0.0.1");
  EXPECT_EQ(s.tws_port, 4002);
  EXPECT_EQ(s.tws_client_id, 7);
  EXPECT_TRUE(s.tws_connected);
}

}  // namespace
}  // namespace dlterm::diag
