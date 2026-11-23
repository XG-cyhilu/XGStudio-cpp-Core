# Copyright 2026 XGstudio
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(XGPROC VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGPROC_VERSION 1.0.0)

# XGPROC —— 通用子进程执行与输出捕获
#
# 选项：
#   XGPROC_BUILD_DEMO  构建命令行示例（默认开）

option(XGPROC_BUILD_DEMO "Build the XGPROC demo" ON)

add_library(xgproc STATIC src/xgproc.cpp)
add_library(XGPROC::xgproc ALIAS xgproc)

target_compile_features(xgproc PUBLIC cxx_std_17)
target_include_directories(xgproc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(xgproc PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xgproc PRIVATE /utf-8 /W3)
target_link_libraries(xgproc PUBLIC synchronization)
set_target_properties(xgproc PROPERTIES DEBUG_POSTFIX d)

if (XGPROC_BUILD_DEMO)
    add_executable(xgproc_demo demo/main.cpp)
    target_link_libraries(xgproc_demo PRIVATE XGPROC::xgproc)
    target_compile_definitions(xgproc_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgproc_demo PRIVATE /utf-8 /W3)
endif()

include(GNUInstallDirs)
install(TARGETS xgproc EXPORT XGPROCTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xgproc DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XGPROCTargets NAMESPACE XGPROC:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgproc)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xgprocConfigVersion.cmake"
    VERSION ${XGPROC_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgprocConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/XGPROCTargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xgprocConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xgprocConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgproc)
