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

# ctest 套件：每个模块一个小 exe，全部只依赖本仓库 + 系统库，可在无网 CI 上跑。
# 只有目标存在才注册，所以 -DCPPLOCLIB_BUILD_XXX=OFF 后不会因为缺目标而配置失败。

function(xgt_add_test name module)
    add_executable(xgt_${name} ${name}/main.cpp)
    target_link_libraries(xgt_${name} PRIVATE ${module})
    target_include_directories(xgt_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(xgt_${name} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xgt_${name} PRIVATE /utf-8 /W3)
    add_test(NAME xgt_${name} COMMAND xgt_${name})
    set_tests_properties(xgt_${name} PROPERTIES TIMEOUT 120)
endfunction()

if (TARGET XGCFG::xgcfg)
    xgt_add_test(cfg XGCFG::xgcfg)
endif()

if (TARGET XGFS::xgfs)
    xgt_add_test(fs XGFS::xgfs)
endif()

if (TARGET XGLOG::xglog)
    xgt_add_test(log XGLOG::xglog)
endif()

if (TARGET XGCRYPTO::xgcrypto)
    xgt_add_test(crypto XGCRYPTO::xgcrypto)
endif()

if (TARGET XGSECURE::xgsecure)
    xgt_add_test(secure XGSECURE::xgsecure)
endif()

if (TARGET XGTHREAD::xgthread)
    xgt_add_test(thread XGTHREAD::xgthread)
endif()

if (TARGET XGZIP::xgzip)
    xgt_add_test(zip XGZIP::xgzip)
endif()

if (TARGET XGSHELL::xgshell)
    xgt_add_test(shell XGSHELL::xgshell)
endif()

if (TARGET XGPROC::xgproc)
    xgt_add_test(proc XGPROC::xgproc)
endif()

if (TARGET XGCRASH::xgcrash)
    xgt_add_test(crash XGCRASH::xgcrash)
endif()

if (TARGET XGNET::xgnet)
    xgt_add_test(net XGNET::xgnet)
endif()

if (TARGET XGCORE::xgcore)
    xgt_add_test(core XGCORE::xgcore)
endif()

# XGDL 的自检已经写在 demo 里（自带回环 HTTP 服务，不需要外网），直接拿来当一条测试。
if (TARGET xgdl_demo)
    add_test(NAME xgt_download_selfcheck COMMAND xgdl_demo --selfcheck)
    set_tests_properties(xgt_download_selfcheck PROPERTIES TIMEOUT 180)
endif()
