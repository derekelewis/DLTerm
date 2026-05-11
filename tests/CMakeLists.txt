# GoogleTest via FetchContent.
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_SHALLOW TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Core test sources: anything in unit/ or screen/ ending in _test.cc,
# EXCEPT unit/services/* (which need the TWS API headers).
file(GLOB_RECURSE DLTERM_TEST_CORE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/*_test.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/screen/*_test.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/support/*.cc"
)
file(GLOB_RECURSE DLTERM_TEST_TWS_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/services/*_test.cc"
)
list(REMOVE_ITEM DLTERM_TEST_CORE_SOURCES ${DLTERM_TEST_TWS_SOURCES})

set(DLTERM_TEST_SOURCES ${DLTERM_TEST_CORE_SOURCES})
if(TARGET dlterm_tws)
  list(APPEND DLTERM_TEST_SOURCES ${DLTERM_TEST_TWS_SOURCES})
endif()

add_executable(dlterm_tests ${DLTERM_TEST_SOURCES})
target_include_directories(dlterm_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(dlterm_tests PRIVATE
  dlterm_core
  GTest::gtest
  GTest::gmock
  GTest::gtest_main
)
target_compile_options(dlterm_tests PRIVATE
  -Wall -Wextra -Wpedantic -Wno-unused-parameter
)

# Link the TWS layer too, when present, so service tests can build.
if(TARGET dlterm_tws)
  target_link_libraries(dlterm_tests PRIVATE dlterm_tws)
  # The TWS dylibs are copied into ${CMAKE_BINARY_DIR}; tests live one
  # directory deeper, so reach back up via @executable_path/..
  set_target_properties(dlterm_tests PROPERTIES
    BUILD_RPATH "@executable_path/.."
    INSTALL_RPATH "@executable_path/.."
  )
endif()

gtest_discover_tests(dlterm_tests)
