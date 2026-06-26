# Kernel module — playtime_k.skprx

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O2 -std=gnu99 -nostartfiles")

add_executable(playtime_k main.c)

target_link_libraries(playtime_k
  SceSysmemForDriver_stub
  SceProcessmgrForDriver_stub
  SceProcessmgrForKernel_stub
  SceSysrootForKernel_stub
  SceIofilemgrForDriver_stub
  SceThreadmgrForDriver_stub
  SceSysclibForDriver_stub
  SceDebugForDriver_stub
  taihenForKernel_stub
  gcc
)

vita_create_self(playtime_k.skprx playtime_k
  UNSAFE
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/playtime_k.yml
)
