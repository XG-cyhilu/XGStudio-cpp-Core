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
    project(XGNET VERSION 1.0.0 LANGUAGES CXX)
endif()

set(XGNET_VERSION 1.0.0)

# XGNET —— 同步 HTTP 客户端（libcurl easy + 连接复用）
#
# 选项：
#   XGNET_BUILD_DEMO  构建示例（默认开）
#   XGNET_CURL_DIR    静态 libcurl 安装前缀；留空则用 find_package(CURL)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(XGNET_BUILD_DEMO "Build the XGNET demo" ON)
set(XGNET_CURL_DIR "" CACHE PATH
    "Prefix of a static libcurl install (include/curl/curl.h + lib/libcurl.lib)；留空自动探测")

if (XGNET_CURL_DIR AND EXISTS "${XGNET_CURL_DIR}/include/curl/curl.h")
    set(CURL_INCLUDE_DIR "${XGNET_CURL_DIR}/include")
    find_library(CURL_LIBRARY_VAR NAMES libcurl curl PATHS "${XGNET_CURL_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
    set(CURL_LIBRARY "${CURL_LIBRARY_VAR}")
else()
    # 自动探测：仓库根的 .deps，或 XGDL 自己编译出来的那份
    foreach(cand
            "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/curl-install"
            "${CMAKE_CURRENT_SOURCE_DIR}/../modDownload/.deps/curl-install")
        if (EXISTS "${cand}/include/curl/curl.h")
            set(CURL_INCLUDE_DIR "${cand}/include")
            find_library(CURL_LIBRARY_VAR NAMES libcurl curl PATHS "${cand}/lib" NO_DEFAULT_PATH)
            if (CURL_LIBRARY_VAR)
                set(CURL_LIBRARY "${CURL_LIBRARY_VAR}")
                message(STATUS "XGNET: using libcurl at ${cand}")
                break()
            endif()
        endif()
    endforeach()
endif()

if (NOT CURL_LIBRARY)
    find_package(CURL REQUIRED)
    set(CURL_INCLUDE_DIR ${CURL_INCLUDE_DIRS})
    set(CURL_LIBRARY ${CURL_LIBRARIES})
endif()

add_library(xgnet STATIC src/xgnet.cpp src/gzip.cpp)
add_library(XGNET::xgnet ALIAS xgnet)

target_compile_features(xgnet PUBLIC cxx_std_17)
target_include_directories(xgnet PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE ${CURL_INCLUDE_DIR})
target_compile_definitions(xgnet PRIVATE CURL_STATICLIB UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xgnet PRIVATE /utf-8 /W3)
target_link_libraries(xgnet PUBLIC
    $<BUILD_INTERFACE:${CURL_LIBRARY}>
    $<INSTALL_INTERFACE:$<LINK_ONLY:CURL::libcurl>>
    bcrypt ws2_32 crypt32 secur32 wldap32)
set_target_properties(xgnet PROPERTIES DEBUG_POSTFIX d)

if (XGNET_BUILD_DEMO)
    add_executable(xgnet_demo demo/main.cpp)
    target_link_libraries(xgnet_demo PRIVATE XGNET::xgnet)
    target_compile_definitions(xgnet_demo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgnet_demo PRIVATE /utf-8 /W3)
endif()

include(GNUInstallDirs)
install(TARGETS xgnet EXPORT XGNETTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/xgnet DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XGNETTargets NAMESPACE XGNET:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgnet)

# 把静态 libcurl 一起装进前缀：消费方不需要系统里有 CURL，也不需要 vcpkg
install(FILES "${CURL_LIBRARY}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
if (CURL_INCLUDE_DIR AND EXISTS "${CURL_INCLUDE_DIR}/curl/curl.h")
    install(DIRECTORY "${CURL_INCLUDE_DIR}/curl" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xgnetConfigVersion.cmake"
    VERSION ${XGNET_VERSION}
    COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xgnetConfig.cmake"
     "if(NOT TARGET CURL::libcurl)\n"
     "  add_library(CURL::libcurl STATIC IMPORTED)\n"
     "  get_filename_component(_xgnet_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
     "  set_target_properties(CURL::libcurl PROPERTIES\n"
     "    IMPORTED_LOCATION \"\${_xgnet_prefix}/lib/libcurl.lib\"\n"
     "    INTERFACE_INCLUDE_DIRECTORIES \"\${_xgnet_prefix}/include\")\n"
     "endif()\n"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/XGNETTargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xgnetConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xgnetConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgnet)
