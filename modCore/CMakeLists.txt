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
    project(XGCORE VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGCORE_VERSION 1.0.0)

# XGCORE —— 聚合头 + 版本信息
#
# 把本次构建里编进来的模块一次性链上，这样 <xgcore/xgcore.h> 的 __has_include
# 能看见它们的公开头。只链"已存在"的目标，所以关掉某个 CPPLOCLIB_BUILD_* 也不会报错。
#
# 选项：
#   XGCORE_BUILD_DEMO  构建示例（默认开）

option(XGCORE_BUILD_DEMO "Build the XGCORE demo" ON)

add_library(xgcore STATIC src/xgcore.cpp)
add_library(XGCORE::xgcore ALIAS xgcore)

target_compile_features(xgcore PUBLIC cxx_std_17)
target_include_directories(xgcore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(xgcore PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xgcore PRIVATE /utf-8 /W3)
set_target_properties(xgcore PROPERTIES DEBUG_POSTFIX d)

set(_xgcore_modules "")
foreach (_m IN ITEMS XGDL::xgdl XGLOG::xglog XGPROC::xgproc XGCRYPTO::xgcrypto
                        XGZIP::xgzip XGFS::xgfs XGCFG::xgcfg XGTHREAD::xgthread
                        XGNET::xgnet XGSECURE::xgsecure XGSHELL::xgshell XGCRASH::xgcrash)
    if (TARGET ${_m})
        target_link_libraries(xgcore PUBLIC ${_m})
        string(TOLOWER ${_m} _lower)
        string(REGEX REPLACE "::.*" "" _mod ${_lower})
        string(APPEND _xgcore_modules "\"${_mod}\",")
    endif()
endforeach()

if (NOT _xgcore_modules STREQUAL "")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgcore_modules.h"
         "#pragma once\n#define XGCORE_MODULE_LIST ${_xgcore_modules}\n")
    target_include_directories(xgcore PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()

if (XGCORE_BUILD_DEMO)
    add_executable(xgcore_demo demo/main.cpp)
    target_link_libraries(xgcore_demo PRIVATE XGCORE::xgcore)
    target_compile_definitions(xgcore_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgcore_demo PRIVATE /utf-8 /W3)
endif()

include(GNUInstallDirs)
install(TARGETS xgcore EXPORT XGCORETargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xgcore DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XGCORETargets NAMESPACE XGCORE:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgcore)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xgcoreConfigVersion.cmake"
    VERSION ${XGCORE_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgcoreConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/XGCORETargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xgcoreConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xgcoreConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgcore)
