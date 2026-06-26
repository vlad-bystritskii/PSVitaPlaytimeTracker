project(trophymount_k C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O3 -nostdlib")

add_executable(trophymount_k main.c)

target_link_libraries(trophymount_k
  SceSysclibForDriver_stub
  SceSysmemForDriver_stub
  SceModulemgrForDriver_stub
  SceThreadmgrForDriver_stub
  SceProcessmgrForDriver_stub
  SceIofilemgrForDriver_stub
  taihenForKernel_stub
  taihenModuleUtils_stub
)

vita_create_self(trophymount.skprx trophymount_k CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/exports.yml UNSAFE)

vita_create_stubs(trophymount_stubs trophymount_k ${CMAKE_CURRENT_SOURCE_DIR}/exports.yml KERNEL)
