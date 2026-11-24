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
    project(XGCRASH VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGCRASH_VERSION 1.0.0)

# XGCRASH —— 崩溃转储：SEH / std::terminate / VEH 三路汇到 MiniDumpWriteDump
#
# 选项：
#   XGCRASH_BUILD_DEMO  构建示例（默认开）

option(XGCRASH_BUILD_DEMO "Build the XGCRASH demo" ON)

add_library(xgcrash STATIC src/xgcrash.cpp)
add_library(XGCRASH::xgcrash ALIAS xgcrash)

target_compile_features(xgcrash PUBLIC cxx_std_17)
target_include_directories(xgcrash PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(xgcrash PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xgcrash PRIVATE /utf-8 /W3)
target_link_libraries(xgcrash PUBLIC dbghelp ole32 shell32)
set_target_properties(xgcrash PROPERTIES DEBUG_POSTFIX d)

if (XGCRASH_BUILD_DEMO)
    add_executable(xgcrash_demo demo/main.cpp)
    target_link_libraries(xgcrash_demo PRIVATE XGCRASH::xgcrash)
    target_compile_definitions(xgcrash_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgcrash_demo PRIVATE /utf-8 /W3)
endif()

include(GNUInstallDirs)
install(TARGETS xgcrash EXPORT XGCRASHTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xgcrash DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XGCRASHTargets NAMESPACE XGCRASH:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgcrash)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xgcrashConfigVersion.cmake"
    VERSION ${XGCRASH_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgcrashConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/XGCRASHTargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xgcrashConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xgcrashConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgcrash)
