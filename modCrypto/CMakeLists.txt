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
    project(XGCRYPTO VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGCRYPTO_VERSION 1.0.0)

# XGCRYPTO —— 摘要计算（MD5 / SHA1 / SHA256），基于系统 CNG
#
# 选项：
#   XGCRYPTO_BUILD_DEMO  构建示例（默认开）

option(XGCRYPTO_BUILD_DEMO "Build the XGCRYPTO demo" ON)

add_library(xgcrypto STATIC src/xgcrypto.cpp)
add_library(XGCRYPTO::xgcrypto ALIAS xgcrypto)

target_compile_features(xgcrypto PUBLIC cxx_std_17)
target_include_directories(xgcrypto PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(xgcrypto PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xgcrypto PRIVATE /utf-8 /W3)
target_link_libraries(xgcrypto PUBLIC bcrypt)
set_target_properties(xgcrypto PROPERTIES DEBUG_POSTFIX d)

if (XGCRYPTO_BUILD_DEMO)
    add_executable(xgcrypto_demo demo/main.cpp)
    target_link_libraries(xgcrypto_demo PRIVATE XGCRYPTO::xgcrypto)
    target_compile_definitions(xgcrypto_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgcrypto_demo PRIVATE /utf-8 /W3)
endif()

include(GNUInstallDirs)
install(TARGETS xgcrypto EXPORT XGCRYPTOTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xgcrypto DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XGCRYPTOTargets NAMESPACE XGCRYPTO:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgcrypto)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xgcryptoConfigVersion.cmake"
    VERSION ${XGCRYPTO_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgcryptoConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/XGCRYPTOTargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xgcryptoConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xgcryptoConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgcrypto)
