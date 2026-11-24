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
    project(XGDL VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGDL_VERSION 1.0.0)

# XGDL —— Windows 上的多路复用下载库（libcurl CURLM + 线程池 + 分块调度）
#
# 选项：
#   XGDL_BUILD_DEMO   构建命令行示例（默认开）
#   XGDL_CURL_DIR     静态 libcurl 安装前缀；留空则用 find_package(CURL)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(XGDL_BUILD_DEMO "Build the XGDL command line demo" ON)
set(XGDL_CURL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/curl-install" CACHE PATH
    "Prefix of a static libcurl install (include/curl/curl.h + lib/libcurl.lib)")

if (EXISTS "${XGDL_CURL_DIR}/include/curl/curl.h")
    set(CURL_INCLUDE_DIR "${XGDL_CURL_DIR}/include")
    find_library(CURL_LIBRARY_VAR NAMES libcurl curl
                 PATHS "${XGDL_CURL_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
    set(CURL_LIBRARY "${CURL_LIBRARY_VAR}")
else()
    find_package(CURL REQUIRED)
    set(CURL_INCLUDE_DIR ${CURL_INCLUDE_DIRS})
    set(CURL_LIBRARY ${CURL_LIBRARIES})
endif()

set(XGDL_SOURCES
    src/Engine.cpp
    src/Manager.cpp
    src/Util.cpp)

add_library(xgdl STATIC ${XGDL_SOURCES})
add_library(XGDL::xgdl ALIAS xgdl)

target_compile_features(xgdl PUBLIC cxx_std_17)
target_include_directories(xgdl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE src ${CURL_INCLUDE_DIR})
target_compile_definitions(xgdl PRIVATE CURL_STATICLIB UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xgdl PRIVATE /utf-8 /W3)
# 静态 libcurl（Schannel 后端）需要的系统库。
# CURL_LIBRARY 在本地是 .lib 路径、在 vcpkg 下是 CURL::libcurl 目标名；直接进 PUBLIC，
# 不包 $<BUILD_INTERFACE>/<INSTALL_INTERFACE>——vcpkg 的 curl 接口自带 $<LINK_ONLY:...>，
# 套进 genex 会在 generate 阶段报"may only be used for linking"。
target_link_libraries(xgdl PUBLIC
    ${CURL_LIBRARY}
    bcrypt ws2_32 crypt32 secur32 wldap32)
set_target_properties(xgdl PROPERTIES DEBUG_POSTFIX d)

if (XGDL_BUILD_DEMO)
    add_executable(xgdl_demo demo/main.cpp)
    target_link_libraries(xgdl_demo PRIVATE XGDL::xgdl)
    target_compile_definitions(xgdl_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgdl_demo PRIVATE /utf-8 /W3)
endif()

include(GNUInstallDirs)
install(TARGETS xgdl EXPORT xgdlTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xgdl DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT xgdlTargets NAMESPACE XGDL:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgdl)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xgdlConfigVersion.cmake"
    VERSION ${XGDL_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgdlConfig.cmake"
     "include(CMakeFindDependencyMacro)\n"
     "find_dependency(CURL)\n"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/xgdlTargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xgdlConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xgdlConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgdl)
