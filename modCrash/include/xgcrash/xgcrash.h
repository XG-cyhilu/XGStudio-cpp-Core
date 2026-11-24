// Copyright 2026 XGstudio
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xgcrash {

// 崩溃处理：未处理异常（SEH）+ std::terminate 都落一个 minidump，
// 目录里只保留最近 maxDumps 个，避免磁盘被撑爆。
//
// 用法（main 里最先调用）：
//   xgcrash::Options opt;          // 默认目录 %LOCALAPPDATA%\<appName>\CrashDumps
//   xgcrash::Install(opt);
struct Options {
    std::wstring dumpDir;          // 空 = DefaultDumpDir()
    std::wstring appName;          // 空 = 当前 exe 名（不含扩展名）
    uint32_t maxDumps = 10;        // 0 = 不清理
    bool fullMemory = false;       // true 生成完整内存转储（很大，默认只看栈）
    bool catchTerminate = true;    // 是否接管 std::terminate
    bool catchVectored = false;    // 是否加 VEH（能抓到 SEH 抓不到的部分崩溃，代价是稍慢）
    std::function<void(const std::wstring& dumpPath)> onWritten;   // 写完后回调（同线程）
};

// 注册处理器。重复调用会先撤掉旧的再装新的。
bool Install(const Options& opt);

// 恢复之前的处理器（测试或正常退出时用）。进程正常退出不需要调用。
void Shutdown();

// 默认转储目录：%LOCALAPPDATA%\<app>\CrashDumps
std::wstring DefaultDumpDir();

// 给一个已经捕获的异常写 dump。exceptionPointers 可为 nullptr（只写普通内存转储）。
// outPath 非空时回填实际落盘路径。
bool WriteDump(void* exceptionPointers, const std::wstring& tag, std::wstring* outPath = nullptr);

// 目录里现有 dump（按时间从旧到新）
std::vector<std::wstring> ListDumps();

// 按 maxDumps 清理旧 dump，返回删除数量
size_t PruneDumps(uint32_t maxDumps);

} // namespace xgcrash
