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

# cpploclib —— XGstudio 的 C++ 公共组件聚合（每个子目录一个独立可用的库）
#
#   modDownload/   XGDL，多路复用下载库          → 目标 XGDL::xgdl
#
# 用法（二选一）：
#   add_subdirectory(cpploclib)
#   target_link_libraries(YourApp PRIVATE XGDL::xgdl)
#
# 或者单独使用某个模块目录，各模块自身也能独立 configure。

cmake_minimum_required(VERSION 3.20)
project(cpploclib VERSION 1.0.0 LANGUAGES CXX)

option(CPPLOCLIB_BUILD_DOWNLOAD "Build the XGDL download module" ON)
option(CPPLOCLIB_BUILD_LOG "Build the XGLOG logging module" ON)
option(CPPLOCLIB_BUILD_PROC "Build the XGPROC subprocess module" ON)
option(CPPLOCLIB_BUILD_CRYPTO "Build the XGCRYPTO digest module" ON)
option(CPPLOCLIB_BUILD_ZIP "Build the XGZIP archive module" ON)
option(CPPLOCLIB_BUILD_FS "Build the XGFS file/path module" ON)
option(CPPLOCLIB_BUILD_CFG "Build the XGCFG config module" ON)
option(CPPLOCLIB_BUILD_THREAD "Build the XGTHREAD pool/queue module" ON)
option(CPPLOCLIB_BUILD_NET "Build the XGNET http client module" ON)
option(CPPLOCLIB_BUILD_SECURE "Build the XGSECURE encoding/crypto module" ON)
option(CPPLOCLIB_BUILD_SHELL "Build the XGSHELL Windows shell integration module" ON)
option(CPPLOCLIB_BUILD_CRASH "Build the XGCRASH minidump module" ON)
option(CPPLOCLIB_BUILD_DEMOS "Build module demos" ON)

if (CPPLOCLIB_BUILD_DOWNLOAD)
    set(XGDL_BUILD_DEMO ${CPPLOCLIB_BUILD_DEMOS} CACHE BOOL "" FORCE)
    add_subdirectory(modDownload)
endif()

if (CPPLOCLIB_BUILD_LOG)
    set(XGLOG_BUILD_DEMO ${CPPLOCLIB_BUILD_DEMOS} CACHE BOOL "" FORCE)
    add_subdirectory(modLog)
endif()

if (CPPLOCLIB_BUILD_PROC)
    set(XGPROC_BUILD_DEMO ${CPPLOCLIB_BUILD_DEMOS} CACHE BOOL "" FORCE)
    add_subdirectory(modProc)
endif()

if (CPPLOCLIB_BUILD_CRYPTO)
    set(XGCRYPTO_BUILD_DEMO ${CPPLOCLIB_BUILD_DEMOS} CACHE BOOL "" FORCE)
    add_subdirectory(modCrypto)
endif()

if (CPPLOCLIB_BUILD_ZIP)
    set(XGZIP_BUILD_DEMO ${CPPLOCLIB_BUILD_DEMOS} CACHE BOOL "" FORCE)
    add_subdirectory(modZip)
endif()

if (CPPLOCLIB_BUILD_FS)
    set(XGFS_BUILD_DEMO ${CPPLOCLIB_BUILD_DEMOS} CACHE BOOL "" FORCE)
    add_subdirectory(modFs)
endif()

if (CPPLOCLIB_BUILD_CFG)
    set(XGCFG_BUILD_DEMO ${CPPLOCLIB_BUILD_DEMOS} CACHE BOOL "" FORCE)
    add_subdirectory(modCfg)
endif()

if (CPPLOCLIB_BUILD_THREAD)
    set(XGTHREAD_BUILD_DEMO ${CPPLOCLIB_BUILD_DEMOS} CACHE BOOL "" FORCE)
    add_subdirectory(modThread)
endif()

if (CPPLOCLIB_BUILD_NET)
    set(XGNET_BUILD_DEMO ${CPPLOCLIB_BUILD_DEMOS} CACHE BOOL "" FORCE)
    add_subdirectory(modNet)
endif()

if (CPPLOCLIB_BUILD_SECURE)
    set(XGSECURE_BUILD_DEMO ${CPPLOCLIB_BUILD_DEMOS} CACHE BOOL "" FORCE)
    add_subdirectory(modSecure)
endif()

if (CPPLOCLIB_BUILD_SHELL)
    set(XGSHELL_BUILD_DEMO ${CPPLOCLIB_BUILD_DEMOS} CACHE BOOL "" FORCE)
    add_subdirectory(modShell)
endif()

if (CPPLOCLIB_BUILD_CRASH)
    set(XGCRASH_BUILD_DEMO ${CPPLOCLIB_BUILD_DEMOS} CACHE BOOL "" FORCE)
    add_subdirectory(modCrash)
endif()
