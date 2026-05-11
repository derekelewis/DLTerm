# Testing

dlterm has a GoogleTest suite in `tests/`. The build splits sources
into a SDL/TWS-free static lib (`dlterm_core`), a TWS-only static lib
(`dlterm_tws`), and the executable (`dlterm`). Tests link `dlterm_core`
unconditionally and `dlterm_tws` when present.

## Run

```bash
# Configure with tests enabled (default).
cmake -B build
cmake --build build -j --target dlterm_tests

# Run.
ctest --test-dir build --output-on-failure
```

For a CI-style build that omits the SDL/TWS app entirely:

```bash
cmake -B build-ci -DDLTERM_BUILD_APP=OFF
cmake --build build-ci -j
ctest --test-dir build-ci --output-on-failure
```

## Layout

```
tests/
  CMakeLists.txt           — fetches GoogleTest, globs *_test.cc
  unit/                    — pure-logic tests (no SDL window, no TWS)
    log_test.cc
    command_parser_test.cc
    widgets_math_test.cc
    layout_test.cc
    diagnostics_test.cc
    services/
      chart_service_test.cc
      portfolio_service_test.cc
  screen/                  — screen-level tests using fakes/spies
    widgets_render_test.cc
    function_bar_render_test.cc
    news_screen_test.cc
    portfolio_screen_test.cc
    chart_screen_test.cc
    debug_screen_test.cc
  support/                 — test fakes + spies (linked into all tests)
    spy_render_backend.{h,cc}
    fake_ibkr_client.h
```

`tests/CMakeLists.txt` globs `**/*_test.cc` and `support/*.cc`. New
test files just need to live under `unit/` or `screen/` with the
`_test.cc` suffix — re-run cmake to refresh the glob.

## Test infrastructure

### `SpyRenderBackend` — recording RenderBackend

`tests/support/spy_render_backend.{h,cc}`. Implements the
`RenderBackend` interface with a fixed grid; every draw call gets
recorded into a `std::vector<Call>`. `DrawTextLine` also writes the
text into a virtual character grid you can read back via
`Row(int)` / `GridSnapshot()`.

```cpp
test::SpyRenderBackend backend(120, 30);  // cols, rows
TextBlock block;
block.SetLines({"hello", "world"});
ScreenContext ctx{ .render = &backend, .metrics = backend.Metrics() };
block.Render(ctx);
EXPECT_EQ(backend.Row(2), "hello");      // first body row
EXPECT_EQ(backend.TextCount(), 2u);
```

For pixel-precise primitives (FillRect, lines, triangles), use the
counters: `FillRectCount()`, `LineSegmentCount()`, etc., or walk
`Calls()` directly.

### `FakeIbkrClient` — in-memory IIbkrClient

`tests/support/fake_ibkr_client.h`. Implements every `IIbkrClient`
method against in-memory state. Tests push canned data with helpers
like `PushHeadline`, `SetPortfolio`, `SetChart`, `SetInfo`, then
assert the screen renders it correctly.

```cpp
test::FakeIbkrClient client;
client.SetConnected(true);
client.PushHeadline({.ts = ..., .provider = "BRFG", .headline = "..."});

NewsScreen screen("AAPL");
ScreenContext ctx{ .client = &client, ... };
screen.OnEnter(ctx);
EXPECT_EQ(client.subscribe_news_calls, 1);
```

The fake counts every method call (`subscribe_news_calls`,
`request_chart_calls`, etc.) and stores the most recent argument on
named members (`last_news_symbol`, `last_chart_symbol`, …).

## Writing a new test

```cpp
// tests/unit/foo_test.cc
#include "foo.h"
#include <gtest/gtest.h>

TEST(Foo, DoesTheThing) {
  EXPECT_EQ(Foo{}.Bar(), 42);
}
```

1. Create `tests/unit/<name>_test.cc` (or `tests/screen/`).
2. Re-run cmake (`cmake -B build`) to refresh the file glob.
3. `cmake --build build -j --target dlterm_tests && ctest --test-dir build`.

For screen-level tests, use the fixture pattern:

```cpp
class MyScreenTest : public ::testing::Test {
 protected:
  test::FakeIbkrClient client;
  test::SpyRenderBackend backend{120, 30};
  FunctionBar bar;

  ScreenContext MakeCtx() {
    ScreenContext ctx;
    ctx.render = &backend;
    ctx.metrics = backend.Metrics();
    ctx.win_w_px = backend.WindowWidthPx();
    ctx.win_h_px = backend.WindowHeightPx();
    ctx.pixel_density = backend.PixelDensity();
    ctx.client = &client;
    ctx.bar = &bar;
    return ctx;
  }
};
```

## Service tests

Services take a `ServiceContext&`; tests construct one with
`client = nullptr`. The services check for null before calling
`EClient` methods, so they record state from the EWrapper hooks
(`OnHistoricalData`, `OnPositionMulti`, …) without needing a live
TWS connection.

See `tests/unit/services/chart_service_test.cc` for the canonical
pattern: instantiate the service, call its public Subscribe/Request
method, then call the EWrapper hooks directly to simulate TWS
callbacks, then assert via `Snapshot()`.

## Logger tests

`tests/unit/log_test.cc`. Uses `dlterm::log::Shutdown()` in
`SetUp/TearDown` to start each test from a clean state, plus
`SetFilterForTesting(...)` to inject a filter without depending on
env vars.

## What's deliberately NOT tested in unit tests

- **Live TWS connection**: covered by `tws_smoketest` (manual,
  requires a running TWS/Gateway).
- **SDL rendering output**: the SDL backend is pass-through to SDL
  primitives; we test `widgets.cc` / `function_bar.cc` against the
  spy backend instead.
- **Font rendering / glyph cache**: the `GlyphCache` and `DrawLine`
  in `text_grid.cc` are SDL-resource code; visually verified via the
  app, not unit-tested.

## Adding the test framework to a new dep

`tests/CMakeLists.txt` fetches GoogleTest via `FetchContent` pinned to
`v1.15.2`. To bump:

```cmake
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG <new-tag>
  GIT_SHALLOW TRUE)
```

`gtest_discover_tests(dlterm_tests)` enumerates each `TEST(...)` /
`TEST_F(...)` at build time and registers it with CTest. No
hand-maintained test list.
