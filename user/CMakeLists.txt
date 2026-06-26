# User module — playtime_u.suprx  (loaded into SceShell)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O2 -std=gnu99 -nostartfiles -DDIAG_NONET=1")

add_executable(playtime_u main.c)

target_link_libraries(playtime_u
  SceLibKernel_stub
  SceIofilemgr_stub
  gcc
)

vita_create_self(playtime_u.suprx playtime_u
  UNSAFE
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/playtime_u.yml
)
