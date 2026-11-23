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
    project(XGSECURE VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGSECURE_VERSION 1.0.0)

# XGSECURE —— Base64 / 随机 / HMAC / AES-GCM / PBKDF2
#
# 选项：
#   XGSECURE_BUILD_DEMO  构建示例（默认开）

option(XGSECURE_BUILD_DEMO "Build the XGSECURE demo" ON)

add_library(xgsecure STATIC src/xgsecure.cpp)
add_library(XGSECURE::xgsecure ALIAS xgsecure)

target_compile_features(xgsecure PUBLIC cxx_std_17)
target_include_directories(xgsecure PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(xgsecure PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xgsecure PRIVATE /utf-8 /W3)
target_link_libraries(xgsecure PUBLIC bcrypt)
set_target_properties(xgsecure PROPERTIES DEBUG_POSTFIX d)

if (XGSECURE_BUILD_DEMO)
    add_executable(xgsecure_demo demo/main.cpp)
    target_link_libraries(xgsecure_demo PRIVATE XGSECURE::xgsecure)
    target_compile_definitions(xgsecure_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgsecure_demo PRIVATE /utf-8 /W3)
endif()

include(GNUInstallDirs)
install(TARGETS xgsecure EXPORT XGSECURETargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xgsecure DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XGSECURETargets NAMESPACE XGSECURE:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgsecure)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xgsecureConfigVersion.cmake"
    VERSION ${XGSECURE_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgsecureConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/XGSECURETargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xgsecureConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xgsecureConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgsecure)
