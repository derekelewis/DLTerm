cmake_minimum_required(VERSION 3.28)
project(dlterm CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

option(DLTERM_BUILD_APP "Build the SDL/TWS app and TWS-dependent libs" ON)
option(DLTERM_BUILD_TESTS "Build the GoogleTest test suite" ON)

set(DLTERM_TWSAPI_DIR
    "/Users/dlewis/Downloads/twsapi_macunix/IBJts/source/cppclient/client"
    CACHE PATH "Path to the IBKR TWS API C++ client directory")

set(DLTERM_HAS_TWS FALSE)
if(EXISTS "${DLTERM_TWSAPI_DIR}/libTwsSocketClient.dylib")
  set(DLTERM_HAS_TWS TRUE)
endif()

if(DLTERM_BUILD_APP AND NOT DLTERM_HAS_TWS)
  message(FATAL_ERROR
      "DLTERM_TWSAPI_DIR='${DLTERM_TWSAPI_DIR}' does not contain "
      "libTwsSocketClient.dylib. Set -DDLTERM_TWSAPI_DIR=... to override "
      "or pass -DDLTERM_BUILD_APP=OFF to skip the app + TWS bits.")
endif()

list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/protobuf")

find_package(SDL3 CONFIG REQUIRED)

set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  SDL3_ttf
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
  GIT_TAG release-3.2.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3_ttf)

set(DLTERM_FONT_DST ${CMAKE_BINARY_DIR}/assets/fonts/IBMPlexMono-Regular.ttf)
if(NOT EXISTS ${DLTERM_FONT_DST})
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets/fonts)
  message(STATUS "Downloading IBM Plex Mono...")
  file(DOWNLOAD
    https://github.com/IBM/plex/raw/v6.4.0/IBM-Plex-Mono/fonts/complete/ttf/IBMPlexMono-Regular.ttf
    ${DLTERM_FONT_DST}
    SHOW_PROGRESS
    STATUS DLTERM_FONT_STATUS
  )
  list(GET DLTERM_FONT_STATUS 0 DLTERM_FONT_CODE)
  if(NOT DLTERM_FONT_CODE EQUAL 0)
    file(REMOVE ${DLTERM_FONT_DST})
    message(FATAL_ERROR "Failed to download IBM Plex Mono: ${DLTERM_FONT_STATUS}")
  endif()
endif()

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/assets/fonts)
file(COPY ${DLTERM_FONT_DST} DESTINATION ${CMAKE_SOURCE_DIR}/assets/fonts)

# ---------------------------------------------------------------------
# dlterm_core: logic + screens + widgets. Depends on SDL but NOT on
# the TWS API. Includes screens (which only use the IbkrClient public
# header — it has no TWS types). Phase 3 will further split SDL
# rendering out behind a RenderBackend interface.
# ---------------------------------------------------------------------
add_library(dlterm_core STATIC
  src/log.cc
  src/layout.cc
  src/text_grid.cc
  src/sdl_render_backend.cc
  src/diagnostics.cc
  src/function_bar.cc
  src/futures.cc
  src/redact.cc
  src/widgets.cc
  src/body_screen.cc
  src/news_screen.cc
  src/portfolio_screen.cc
  src/chart_screen.cc
  src/quote_screen.cc
  src/debug_screen.cc
  src/splash_screen.cc
  src/screen_factory.cc
)
target_include_directories(dlterm_core PUBLIC src)
target_compile_options(dlterm_core PRIVATE
  -Wall -Wextra -Wpedantic -Wno-unused-parameter
)
target_link_libraries(dlterm_core PUBLIC
  SDL3::SDL3
  SDL3_ttf::SDL3_ttf
)

# ---------------------------------------------------------------------
# dlterm_tws: TWS-API-dependent layer. Built only when TWS headers +
# dylib are available. Provides the live IbkrClient implementation.
# ---------------------------------------------------------------------
if(DLTERM_HAS_TWS)
  find_package(Protobuf REQUIRED CONFIG)
  find_package(ZLIB REQUIRED)

  # API_VersionNum.txt lives at the IBJts root (3 dirs up from the
  # client headers). Parse it once at configure time and bake the
  # marketing version (e.g. "10.46.01") into dlterm_tws.
  set(DLTERM_TWS_API_VERSION "")
  set(_DLTERM_TWS_VERFILE
      "${DLTERM_TWSAPI_DIR}/../../../API_VersionNum.txt")
  if(EXISTS "${_DLTERM_TWS_VERFILE}")
    file(READ "${_DLTERM_TWS_VERFILE}" _DLTERM_TWS_VERTEXT)
    string(REGEX MATCH "API_Version=([0-9.]+)" _unused
        "${_DLTERM_TWS_VERTEXT}")
    set(DLTERM_TWS_API_VERSION "${CMAKE_MATCH_1}")
    message(STATUS "TWS API marketing version: ${DLTERM_TWS_API_VERSION}")
  else()
    message(STATUS "TWS API_VersionNum.txt not found at "
                   "${_DLTERM_TWS_VERFILE}; CLIENT API line on splash "
                   "will omit the marketing version.")
  endif()

  add_library(twsapi_client SHARED IMPORTED)
  set_target_properties(twsapi_client PROPERTIES
      IMPORTED_LOCATION "${DLTERM_TWSAPI_DIR}/libTwsSocketClient.dylib"
      INTERFACE_INCLUDE_DIRECTORIES
          "${DLTERM_TWSAPI_DIR};${DLTERM_TWSAPI_DIR}/protobufUnix")

  add_library(dlterm_tws STATIC
    src/ibkr_client.cc
    src/services/chart_service.cc
    src/services/info_service.cc
    src/services/news_service.cc
    src/services/portfolio_service.cc
    src/services/quote_service.cc
  )
  target_include_directories(dlterm_tws PUBLIC src)
  target_compile_options(dlterm_tws PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
  )
  set_source_files_properties(src/ibkr_client.cc PROPERTIES
    COMPILE_OPTIONS "-w"
  )
  if(DLTERM_TWS_API_VERSION)
    target_compile_definitions(dlterm_tws PRIVATE
      DLTERM_TWS_API_VERSION="${DLTERM_TWS_API_VERSION}")
  endif()
  target_link_libraries(dlterm_tws PUBLIC
    dlterm_core
    twsapi_client
    protobuf::libprotobuf
    ZLIB::ZLIB
  )
endif()

# ---------------------------------------------------------------------
# dlterm: the executable.
# ---------------------------------------------------------------------
if(DLTERM_BUILD_APP)
  add_executable(dlterm src/main.cc)
  target_compile_options(dlterm PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
  )
  target_link_libraries(dlterm PRIVATE dlterm_core dlterm_tws)

  file(COPY
      "${DLTERM_TWSAPI_DIR}/libTwsSocketClient.dylib"
      "${DLTERM_TWSAPI_DIR}/lib/libbid.dylib"
    DESTINATION "${CMAKE_BINARY_DIR}")

  set_target_properties(dlterm PROPERTIES
    BUILD_RPATH "@executable_path"
    INSTALL_RPATH "@executable_path"
  )

  add_executable(tws_smoketest src/tws_smoketest.cc)
  set_source_files_properties(src/tws_smoketest.cc PROPERTIES
    COMPILE_OPTIONS "-w"
  )
  target_link_libraries(tws_smoketest PRIVATE
    twsapi_client
    protobuf::libprotobuf
  )
  set_target_properties(tws_smoketest PROPERTIES
    BUILD_RPATH "@executable_path"
    INSTALL_RPATH "@executable_path"
  )

  add_executable(contfut_smoketest src/contfut_smoketest.cc)
  set_source_files_properties(src/contfut_smoketest.cc PROPERTIES
    COMPILE_OPTIONS "-w"
  )
  target_link_libraries(contfut_smoketest PRIVATE
    twsapi_client
    protobuf::libprotobuf
  )
  set_target_properties(contfut_smoketest PROPERTIES
    BUILD_RPATH "@executable_path"
    INSTALL_RPATH "@executable_path"
  )

  add_executable(futures_news_smoketest src/futures_news_smoketest.cc)
  set_source_files_properties(src/futures_news_smoketest.cc PROPERTIES
    COMPILE_OPTIONS "-w"
  )
  target_link_libraries(futures_news_smoketest PRIVATE
    twsapi_client
    protobuf::libprotobuf
  )
  set_target_properties(futures_news_smoketest PROPERTIES
    BUILD_RPATH "@executable_path"
    INSTALL_RPATH "@executable_path"
  )
endif()

# ---------------------------------------------------------------------
# Layout self-test (legacy harness; superseded by Phase-2 tests).
# ---------------------------------------------------------------------
option(DLTERM_LAYOUT_SELFTEST "Build the legacy layout self-test" OFF)
if(DLTERM_LAYOUT_SELFTEST)
  add_executable(dlterm_layout_selftest src/layout.cc)
  target_compile_definitions(dlterm_layout_selftest PRIVATE DLTERM_LAYOUT_SELFTEST=1)
  target_include_directories(dlterm_layout_selftest PRIVATE src)
endif()

# ---------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------
if(DLTERM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
