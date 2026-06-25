# PSVitaPlaytimeTracker — builds both modules with the VitaSDK toolchain.
#
#   export VITASDK=/usr/local/vitasdk
#   cmake -S . -B build && cmake --build build
#
# Outputs:  build/kernel/playtime_k.skprx   build/user/playtime_u.suprx
# Easiest path is the Docker build — see docs/BUILD.md.

cmake_minimum_required(VERSION 3.2)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Define the VITASDK environment variable (see docs/BUILD.md).")
  endif()
endif()

project(PSVitaPlaytimeTracker C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

add_subdirectory(kernel)
add_subdirectory(user)
