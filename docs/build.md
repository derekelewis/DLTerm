# Build

CMake project (3.28+), C++23. macOS-first; the prebuilt TWS API
dylib pin is Apple-specific.

## Quick reference

```bash
# First-time configure (generates build/ in-tree)
cmake -S . -B build -G Ninja

# Build
cmake --build build -j

# Run
./build/DLTerm

# Run the TWS smoketest (no SDL window)
./build/tws_smoketest
```

## Dependencies

| Dep | How it's pulled in | Version pin |
|---|---|---|
| SDL3 | `find_package(SDL3 CONFIG REQUIRED)` — brew or system install | latest stable |
| SDL3_ttf | `FetchContent` (built from source) | `release-3.2.2` |
| IBM Plex Mono | Downloaded at configure time | `v6.4.0` (regular) |
| TWS API | Imported dylib from `DLTERM_TWSAPI_DIR` | bundled 10.x |
| Protobuf | `find_package(Protobuf REQUIRED CONFIG)` — Homebrew | 34.1 |
| Abseil | Transitive via Protobuf | bundled |
| ZLIB | `find_package(ZLIB REQUIRED)` — system | macOS system |

### Why Protobuf

The TWS API ships protobuf-generated headers in
`$DLTERM_TWSAPI_DIR/protobufUnix`. Linking against
`protobuf::libprotobuf` is required even though we never write
proto code ourselves — the API's binary protocol is wire-encoded as
proto.

The protobuf version must match what the prebuilt dylib was compiled
against. Currently 34.1 (Homebrew `protobuf`). Mismatches manifest as
linker errors mentioning `google::protobuf` internals.

If brew updates protobuf out from under you, either reinstall the
specific version or rebuild the TWS API from source against the new
protobuf. The CMake `list(PREPEND CMAKE_PREFIX_PATH
"/opt/homebrew/opt/protobuf")` line is what makes `find_package` see
the Homebrew install.

## Configure-time settings

```cmake
DLTERM_TWSAPI_DIR  → path to the TWS API C++ client tree.
                     Default: /Users/dlewis/Downloads/twsapi_macunix/IBJts/source/cppclient/client
                     Must contain libTwsSocketClient.dylib.
                     Override with: -DDLTERM_TWSAPI_DIR=/path
```

The build fails fast (`message(FATAL_ERROR ...)`) if
`libTwsSocketClient.dylib` is missing from that directory.

CMake also reads `${DLTERM_TWSAPI_DIR}/../../../API_VersionNum.txt`
(at the IBJts distribution root) to bake the marketing version
(e.g. `10.46.01`) into `dlterm_tws` as
`DLTERM_TWS_API_VERSION`. Shown on the splash's `CLIENT API` line.
If the file is absent, the splash falls back to just the protocol
range and configure logs a `STATUS` note.

## TWS API integration

We **do not** build the TWS API from source. CMake imports the
prebuilt dylibs:

- `libTwsSocketClient.dylib` — the actual client library.
- `lib/libbid.dylib` — Intel BID floating-point library (transitive).

Both get copied to `${CMAKE_BINARY_DIR}` next to the `DLTerm` binary,
and the executable's rpath is set to `@executable_path` so they
resolve at runtime. This means you can run the binary in place
(`./build/DLTerm`) without setting `DYLD_LIBRARY_PATH`.

The include path is `$DLTERM_TWSAPI_DIR` plus
`$DLTERM_TWSAPI_DIR/protobufUnix` (for the generated proto headers).

## Targets

| Target | Source | Purpose |
|---|---|---|
| `dlterm_core` (STATIC LIB) | log, layout, widgets, screens, render backend (SDL impl) | Logic + SDL drawing. No TWS dep. |
| `dlterm_tws` (STATIC LIB) | `ibkr_client.cc`, `services/*` | TWS API integration. Built only when the dylib is present. |
| `DLTerm` (EXE) | `src/main.cc` | The app. Links both libs. Built when `DLTERM_BUILD_APP=ON` (default). |
| `dlterm_tests` (EXE) | `tests/**/*_test.cc` + GoogleTest | Test suite. Built when `DLTERM_BUILD_TESTS=ON` (default). |
| `tws_smoketest` (EXE) | `src/tws_smoketest.cc` | Headless TWS connect / status diagnostic. Requires the TWS dylib. |
| `dlterm_layout_selftest` | `src/layout.cc` (with `DLTERM_LAYOUT_SELFTEST=1`) | Legacy; superseded by `tests/unit/layout_test.cc`. Set `-DDLTERM_LAYOUT_SELFTEST=ON`. |

## Configure-time options

```cmake
DLTERM_BUILD_APP    → build the app + tws_smoketest (default ON; needs TWS dylib)
DLTERM_BUILD_TESTS  → build the GoogleTest suite (default ON)
DLTERM_TWSAPI_DIR   → path to TWS API C++ client tree
```

A typical CI / no-TWS build:

```bash
cmake -B build-ci -DDLTERM_BUILD_APP=OFF
cmake --build build-ci -j --target dlterm_tests
ctest --test-dir build-ci --output-on-failure
```

## Compile options

`-Wall -Wextra -Wpedantic -Wno-unused-parameter` on every TU.

**Exception:** `src/ibkr_client.cc` and `src/tws_smoketest.cc` are
built with `COMPILE_OPTIONS "-w"` (warnings suppressed). These TUs
include the TWS API + protobuf headers, which fail badly under
`-Wpedantic` and `-Wshorten-64-to-32`. We isolate the warning noise
to those two files.

## Font

`IBMPlexMono-Regular.ttf` is downloaded at configure time to
`build/assets/fonts/` and copied to `assets/fonts/` in the source
tree (so the running binary can find it via the working-dir path
`assets/fonts/IBMPlexMono-Regular.ttf`).

If `IBMPlexMono-Regular.ttf` already exists at the destination, the
download is skipped. If the download fails the file is deleted and
configure errors out.

## Runtime env vars

These are read by `src/main.cc`:

| Var | Default | Effect |
|---|---|---|
| `DLTERM_TWS_HOST` | `127.0.0.1` | Gateway host. |
| `DLTERM_TWS_PORT` | `4002` | Gateway port. (4002=paper, 4001=live, 7497=TWS paper, 7496=TWS live.) |
| `DLTERM_TWS_CLIENT_ID` | `1` | Client id for `eConnect`. Use a unique id per concurrent client. |
| `DLTERM_LOG` | `info` | Log filter, e.g. `info,tws.dispatch=trace`. See `docs/logging.md`. |
| `DLTERM_LOG_FILE` | (none) | Mirror log lines to this file (append). |
| `DLTERM_REDACT_ACCOUNTS` | `1` | Mask IB account codes with length-preserving asterisks (`DU1234567` → `*********`) in both the splash display and TRACE-level dispatcher log lines. Set `0`/`false`/`no`/`off` to disable. Read once at first use. |

The smoketest reads the TWS vars.

## Common rebuild pitfalls

- **"protobuf version mismatch"**: brew updated protobuf since last
  build. `brew install protobuf@34` and adjust `CMAKE_PREFIX_PATH`,
  or rebuild the TWS API against the new version.
- **"libTwsSocketClient.dylib not found"** at runtime: the dylib
  copy in CMakeLists.txt didn't run, or you moved the binary.
  Re-run cmake configure, or build, or copy the dylib manually.
- **SDL3_ttf compile error**: `FetchContent` fetches the pinned tag.
  Make sure your toolchain supports C++23 (Apple Clang 15+, Xcode 15+).
- **Linker can't find Abseil**: Protobuf 34.1 has Abseil as a
  transitive dep. brew should install both. If `find_package` finds
  protobuf but not Abseil, your `Protobuf-config.cmake` is stale.
- **Font download fails on configure**: GitHub raw URL changed or
  your network is down. Either set up the font yourself at
  `build/assets/fonts/IBMPlexMono-Regular.ttf` or fix the network and
  re-configure.

## Adding a new source file

Two edits:

1. Add it to whichever library it belongs to in `CMakeLists.txt` —
   `dlterm_core` for SDL/widget/screen code, `dlterm_tws` for TWS
   services. The `DLTerm` executable target itself is just
   `src/main.cc`.
2. If the new file pulls in TWS or protobuf headers, also add the
   `set_source_files_properties(... COMPILE_OPTIONS "-w")` exception
   for that file. Otherwise leave it on default warnings — the strict
   set catches real issues.

## Adding a new third-party dependency

Pattern: `find_package` for system/Homebrew packages, `FetchContent`
for source-built dependencies. Avoid vendoring binaries (the TWS API
is the lone exception because we don't have a buildable source tree
for it).

For Homebrew-installed deps that don't ship a CMake config, you may
need `list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/<name>")`
before the `find_package` line — same trick we use for protobuf.

## Layout self-test

A non-default target useful when changing `src/layout.cc`:

```bash
cmake -S . -B build_selftest -DDLTERM_LAYOUT_SELFTEST=ON
cmake --build build_selftest -j
./build_selftest/dlterm_layout_selftest
```

Runs the Knuth-Plass word-wrap implementation against a small set of
fixtures and asserts on output. Worth running before merging any
layout change.

## What we deliberately do NOT do

- No package manager (no Conan, no vcpkg). Adds opacity; we have
  enough deps to manage by hand.
- No CMake presets. The defaults work; if you need to override, pass
  `-D…` on the command line.
- No installer target (`install(...)`). The app is run in place.
- No Linux/Windows support yet. The TWS API import block would need
  alternate filenames (`libTwsSocketClient.so`, `.dll`); revisit if we
  port.
