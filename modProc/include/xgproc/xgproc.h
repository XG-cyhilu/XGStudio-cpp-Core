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
#include <memory>
#include <string>
#include <utility>
#include <vector>

// XGPROC —— 通用子进程执行与输出捕获
//
// 和 XGDL / XGLOG 同一套取向：
//   * 输出由读线程吞进有界队列，调用方 Poll() 取行，不注册回调；
//   * 子进程挂在 Job 对象上，Stop() 一定收掉整棵子孙树，不留孤儿；
//   * 队列满就丢并计数，绝不因为下游不读而卡住子进程（写满管道会让子进程阻塞）。

namespace xgproc {

struct Line {
    int64_t timeMs = 0;
    bool stderrLine = false;
    std::string text;        // 已去掉行尾 CR/LF，默认已按 outputEncoding 转成 UTF-8
};

// 输出字节的编码。Windows 上的控制台程序大多按系统 ANSI/OEM 代码页吐字节，
// 也有不少程序直接写 UTF-8，所以默认 Auto 先按严格的 UTF-8 校验，失败再按 OEM 转。
enum class Encoding {
    Auto,
    Utf8,      // 原样保留字节
    Oem,       // GetOEMCP()
    Ansi,      // GetACP()
};

struct Options {
    std::wstring commandLine;                       // 已自带引号处理的完整命令行
    std::wstring workDir;                           // 空 = 继承当前进程
    std::vector<std::pair<std::wstring, std::wstring>> env;  // 覆盖/追加的变量
    uint32_t queueCapacity = 4096;                  // 输出行队列上限
    uint32_t lineBytes = 8 * 1024;                  // 单行最大字节，超出强制截断成一行
    Encoding outputEncoding = Encoding::Auto;       // 输出字节 → UTF-8 的转换方式
};

struct Result {
    bool started = false;
    bool timedOut = false;
    int exitCode = 0;
    uint64_t dropped = 0;
    std::vector<Line> out;      // 合并的 stdout + stderr（按到达顺序）
};

class Process {
public:
    Process();
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    bool Start(const Options& opt);
    // 便捷重载：命令行 + 工作目录 + 环境
    bool Start(const std::wstring& commandLine, const std::wstring& workDir = L"");

    // 取一行；timeoutMs=0 表示不等待。返回 false 表示暂时没有新行。
    bool Poll(Line& out, uint32_t timeoutMs = 0);

    // 写子进程 stdin（发命令、回答交互式提示）。失败返回 false。
    bool WriteStdin(const std::string& bytes);
    bool WriteStdin(const std::wstring& text);

    bool IsRunning() const;
    bool Wait(uint32_t timeoutMs);      // true = 已退出
    int ExitCode() const;               // 未退出时为 INT32_MIN
    uint32_t Pid() const;
    uint64_t Dropped() const;           // 队列满丢弃的行数

    // 先等 graceMs，仍未退出就 TerminateJobObject（连同子孙）。
    void Stop(uint32_t graceMs = 1500);

    // 读完剩余输出并等待退出，返回汇总结果。
    Result Drain(uint32_t timeoutMs = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 一次性跑完并把输出收进 Result（内部封装 Process + Drain）。
Result Run(const std::wstring& commandLine,
           uint32_t timeoutMs = 30'000,
           const std::wstring& workDir = L"");

// Windows 命令行参数转义（CommandLineToArgvW 语义）；需要时自行拼接用。
std::wstring Quote(const std::wstring& arg);
std::wstring JoinArgs(const std::vector<std::wstring>& args);

} // namespace xgproc
