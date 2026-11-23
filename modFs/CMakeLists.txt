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
    project(XGFS VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGFS_VERSION 1.0.0)

# XGFS —— 文件与路径：规范化、原子写、临时文件、递归遍历
#
# 选项：
#   XGFS_BUILD_DEMO  构建示例（默认开）

option(XGFS_BUILD_DEMO "Build the XGFS demo" ON)

add_library(xgfs STATIC src/xgfs.cpp)
add_library(XGFS::xgfs ALIAS xgfs)

target_compile_features(xgfs PUBLIC cxx_std_17)
target_include_directories(xgfs PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(xgfs PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xgfs PRIVATE /utf-8 /W3)
set_target_properties(xgfs PROPERTIES DEBUG_POSTFIX d)

if (XGFS_BUILD_DEMO)
    add_executable(xgfs_demo demo/main.cpp)
    target_link_libraries(xgfs_demo PRIVATE XGFS::xgfs)
    target_compile_definitions(xgfs_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgfs_demo PRIVATE /utf-8 /W3)
endif()

include(GNUInstallDirs)
install(TARGETS xgfs EXPORT XGFSTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xgfs DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XGFSTargets NAMESPACE XGFS:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgfs)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xgfsConfigVersion.cmake"
    VERSION ${XGFS_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgfsConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/XGFSTargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xgfsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xgfsConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgfs)
