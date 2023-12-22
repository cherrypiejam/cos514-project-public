cmake_minimum_required(VERSION 3.10)
project(gpu_worker_client C CXX ASM)

find_package(PkgConfig)
pkg_check_modules(rpclib REQUIRED IMPORTED_TARGET rpclib)

add_executable(gpu-worker-client client.cpp)
target_link_libraries(gpu-worker-client ${rpclib_LIBRARY_DIRS}/librpc.a)
# add -std=c++11 flag
set_target_properties(gpu-worker-client
  PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
)
