cmake_minimum_required(VERSION 3.20)
project(hawser LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# One file with no dependencies beyond the C library: the same binary then
# runs on any Linux, whatever its distribution. Needs a C library that links
# statically without surprises, which is why the release is built on Alpine.
option(HAWSER_STATIC "Link hawser as one static binary" OFF)
set(HAWSER_VERSION "development" CACHE STRING "The version `hawser version` prints")

find_package(Threads REQUIRED)

# WireGuard's own library for talking to the kernel, taken unchanged from
# wireguard-tools. See third_party/wireguard/README.md.
add_library(wireguard STATIC third_party/wireguard/wireguard.c)
target_include_directories(wireguard PUBLIC third_party/wireguard)

add_library(hawser_core STATIC
  src/address.cpp
  src/commands.cpp
  src/daemon.cpp
  src/hosts.cpp
  src/invite.cpp
  src/key.cpp
  src/machines.cpp
  src/path.cpp
  src/protocol.cpp
  src/settings.cpp
  src/text.cpp
  src/tunnel.cpp
)
target_include_directories(hawser_core PUBLIC src)
target_link_libraries(hawser_core PUBLIC wireguard Threads::Threads)
target_compile_options(hawser_core PRIVATE -Wall -Wextra)

add_executable(hawser src/main.cpp)
target_link_libraries(hawser PRIVATE hawser_core)
target_compile_options(hawser PRIVATE -Wall -Wextra)
target_compile_definitions(hawser PRIVATE HAWSER_VERSION="${HAWSER_VERSION}")
if(HAWSER_STATIC)
  # -s leaves out the symbol table, which a release has no use for.
  target_link_options(hawser PRIVATE -static -s)
endif()
install(TARGETS hawser RUNTIME DESTINATION bin)

enable_testing()
add_executable(hawser_tests
  tests/test_main.cpp
  tests/test_address.cpp
  tests/test_hosts.cpp
  tests/test_invite.cpp
  tests/test_machines.cpp
  tests/test_path.cpp
  tests/test_plan.cpp
  tests/test_protocol.cpp
  tests/test_settings.cpp
  tests/test_text.cpp
)
target_link_libraries(hawser_tests PRIVATE hawser_core)
target_compile_options(hawser_tests PRIVATE -Wall -Wextra)
if(HAWSER_STATIC)
  target_link_options(hawser_tests PRIVATE -static)
endif()
add_test(NAME hawser_tests COMMAND hawser_tests)
