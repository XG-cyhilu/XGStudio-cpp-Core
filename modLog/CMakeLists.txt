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
    project(XGLOG VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGLOG_VERSION 1.0.0)

# XGLOG —— 异步、零阻塞、可轮询的日志库
#
# 选项：
#   XGLOG_BUILD_DEMO  构建命令行示例（默认开）

option(XGLOG_BUILD_DEMO "Build the XGLOG demo" ON)

add_library(xglog STATIC src/xglog.cpp)
add_library(XGLOG::xglog ALIAS xglog)

target_compile_features(xglog PUBLIC cxx_std_17)
target_include_directories(xglog PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(xglog PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xglog PRIVATE /utf-8 /W3)
# std::filesystem / std::thread 需要的系统库
target_link_libraries(xglog PUBLIC synchronization)
set_target_properties(xglog PROPERTIES DEBUG_POSTFIX d)

if (XGLOG_BUILD_DEMO)
    add_executable(xglog_demo demo/main.cpp)
    target_link_libraries(xglog_demo PRIVATE XGLOG::xglog)
    target_compile_definitions(xglog_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xglog_demo PRIVATE /utf-8 /W3)
endif()

include(GNUInstallDirs)
install(TARGETS xglog EXPORT XGLOGTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xglog DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XGLOGTargets NAMESPACE XGLOG:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xglog)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xglogConfigVersion.cmake"
    VERSION ${XGLOG_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xglogConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/XGLOGTargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xglogConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xglogConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xglog)
