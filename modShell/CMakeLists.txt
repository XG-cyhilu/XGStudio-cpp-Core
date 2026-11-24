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
    project(XGSHELL VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGSHELL_VERSION 1.0.0)

# XGSHELL —— Windows 桌面壳集成：单实例、关联注册、快捷方式、开机自启、托盘、已知文件夹
#
# 选项：
#   XGSHELL_BUILD_DEMO  构建示例（默认开）

option(XGSHELL_BUILD_DEMO "Build the XGSHELL demo" ON)

add_library(xgshell STATIC src/xgshell.cpp)
add_library(XGSHELL::xgshell ALIAS xgshell)

target_compile_features(xgshell PUBLIC cxx_std_17)
target_include_directories(xgshell PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(xgshell PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xgshell PRIVATE /utf-8 /W3)
target_link_libraries(xgshell PUBLIC shell32 ole32 uuid advapi32 user32)
set_target_properties(xgshell PROPERTIES DEBUG_POSTFIX d)

if (XGSHELL_BUILD_DEMO)
    add_executable(xgshell_demo demo/main.cpp)
    target_link_libraries(xgshell_demo PRIVATE XGSHELL::xgshell)
    target_compile_definitions(xgshell_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgshell_demo PRIVATE /utf-8 /W3)
    # 控制台程序也要造窗口，所以按 GUI 子系统链接并自己 AttachConsole
    set_target_properties(xgshell_demo PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

include(GNUInstallDirs)
install(TARGETS xgshell EXPORT XGSHELLTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xgshell DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XGSHELLTargets NAMESPACE XGSHELL:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgshell)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xgshellConfigVersion.cmake"
    VERSION ${XGSHELL_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgshellConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/XGSHELLTargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xgshellConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xgshellConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgshell)
