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
    project(XGTHREAD VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGTHREAD_VERSION 1.0.0)

# XGTHREAD —— 线程池 + 主线程调度队列
#
# 选项：
#   XGTHREAD_BUILD_DEMO  构建示例（默认开）

option(XGTHREAD_BUILD_DEMO "Build the XGTHREAD demo" ON)

add_library(xgthread STATIC src/xgthread.cpp)
add_library(XGTHREAD::xgthread ALIAS xgthread)

target_compile_features(xgthread PUBLIC cxx_std_17)
target_include_directories(xgthread PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(xgthread PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xgthread PRIVATE /utf-8 /W3)
set_target_properties(xgthread PROPERTIES DEBUG_POSTFIX d)

if (XGTHREAD_BUILD_DEMO)
    add_executable(xgthread_demo demo/main.cpp)
    target_link_libraries(xgthread_demo PRIVATE XGTHREAD::xgthread)
    target_compile_definitions(xgthread_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgthread_demo PRIVATE /utf-8 /W3)
endif()

include(GNUInstallDirs)
install(TARGETS xgthread EXPORT XGTHREADTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xgthread DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XGTHREADTargets NAMESPACE XGTHREAD:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgthread)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xgthreadConfigVersion.cmake"
    VERSION ${XGTHREAD_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgthreadConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/XGTHREADTargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xgthreadConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xgthreadConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgthread)
