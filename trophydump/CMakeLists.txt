# trophydump — Vita homebrew that reads on-console trophy UNLOCK STATE and writes
# it as JSON to ux0:data/VitaPlaytime/trophies.json for the HA add-on to pull over
# FTP. The unlock state lives in per-console-encrypted TRPTITLE.DAT; we mount the
# trophy data folder via a tiny kernel plugin (SceAppMgr mount-id 0x12F → OS
# decrypts via sealedkey/F00D) and read the plaintext TRPTITLE.DAT directly.
#
# Build (from this trophydump/ dir):
#   export VITASDK=$HOME/vitasdk
#   cmake -S . -B build && cmake --build build
# Outputs: build/trophydump.vpk (install + run) and
#          build/kernel/trophymount.skprx (copy to ur0:tai/, add under *KERNEL).
cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Define the VITASDK environment variable.")
  endif()
endif()

project(trophydump C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Vita Trophy Dump")
set(VITA_TITLEID  "VPTD00001")
set(VITA_VERSION  "01.00")

add_subdirectory(kernel)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O2 -std=gnu99")

add_executable(trophydump main.c)

target_link_libraries(trophydump
  ${CMAKE_CURRENT_BINARY_DIR}/kernel/libTrophyMount_stub.a
  SceAppMgr_stub
  SceLibKernel_stub
  SceIofilemgr_stub
)
add_dependencies(trophydump trophymount_stubs)

vita_create_self(eboot.bin trophydump UNSAFE)
vita_create_vpk(trophydump.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)
