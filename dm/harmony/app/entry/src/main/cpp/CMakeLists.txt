# DevEco 构建入口：编 dm/harmony/native（新核心 + 平台层）出 libdm.so。
# 本机路径（DM_GEN_DIR、Python3_EXECUTABLE）放在不入库的 local.cmake，见 local.cmake.example。
# DevEco build entry: builds dm/harmony/native (the shared core + platform layer) into libdm.so.
# Machine paths (DM_GEN_DIR, Python3_EXECUTABLE) live in the uncommitted local.cmake, see local.cmake.example.
cmake_minimum_required(VERSION 3.22)
project(dm_entry C CXX ASM)
include(${CMAKE_CURRENT_SOURCE_DIR}/local.cmake OPTIONAL)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../native ${CMAKE_CURRENT_BINARY_DIR}/native)
