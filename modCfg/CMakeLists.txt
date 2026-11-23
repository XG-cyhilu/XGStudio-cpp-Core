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
    project(XGCFG VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGCFG_VERSION 1.0.0)

# XGCFG —— JSON 值 + 配置文件（点路径、默认值、原子保存、热重载）
#
# 选项：
#   XGCFG_BUILD_DEMO  构建示例（默认开）

option(XGCFG_BUILD_DEMO "Build the XGCFG demo" ON)

add_library(xgcfg STATIC src/xgcfg.cpp)
add_library(XGCFG::xgcfg ALIAS xgcfg)

target_compile_features(xgcfg PUBLIC cxx_std_17)
target_include_directories(xgcfg PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(xgcfg PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xgcfg PRIVATE /utf-8 /W3)
set_target_properties(xgcfg PROPERTIES DEBUG_POSTFIX d)

if (XGCFG_BUILD_DEMO)
    add_executable(xgcfg_demo demo/main.cpp)
    target_link_libraries(xgcfg_demo PRIVATE XGCFG::xgcfg)
    target_compile_definitions(xgcfg_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgcfg_demo PRIVATE /utf-8 /W3)
endif()

include(GNUInstallDirs)
install(TARGETS xgcfg EXPORT XGCFGTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xgcfg DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XGCFGTargets NAMESPACE XGCFG:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgcfg)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xgcfgConfigVersion.cmake"
    VERSION ${XGCFG_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgcfgConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/XGCFGTargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xgcfgConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xgcfgConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgcfg)
