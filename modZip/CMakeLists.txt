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
    project(XGZIP VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGZIP_VERSION 1.0.0)

# XGZIP —— zip 读取、解压与写入（自带 deflate 编解码，无第三方依赖）
#
# 选项：
#   XGZIP_BUILD_DEMO  构建命令行示例（默认开）

option(XGZIP_BUILD_DEMO "Build the XGZIP demo" ON)

add_library(xgzip STATIC src/xgzip.cpp src/inflate.cpp src/deflate.cpp src/ZipWrite.cpp)
add_library(XGZIP::xgzip ALIAS xgzip)

target_compile_features(xgzip PUBLIC cxx_std_17)
target_include_directories(xgzip PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(xgzip PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xgzip PRIVATE /utf-8 /W3)
set_target_properties(xgzip PROPERTIES DEBUG_POSTFIX d)

if (XGZIP_BUILD_DEMO)
    add_executable(xgzip_demo demo/main.cpp)
    target_link_libraries(xgzip_demo PRIVATE XGZIP::xgzip)
    target_compile_definitions(xgzip_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgzip_demo PRIVATE /utf-8 /W3)
endif()

include(GNUInstallDirs)
install(TARGETS xgzip EXPORT XGZIPTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xgzip DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XGZIPTargets NAMESPACE XGZIP:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgzip)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xgzipConfigVersion.cmake"
    VERSION ${XGZIP_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgzipConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/XGZIPTargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xgzipConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xgzipConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgzip)
