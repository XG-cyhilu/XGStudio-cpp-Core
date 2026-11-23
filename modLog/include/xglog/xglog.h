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

#include <cstdarg>
#include <cstdint>
#include <string>

// XGLOG —— 异步、零阻塞、可轮询的日志库
//
// 设计取向与 XGDL 一致：
//   * 调用线程只做一次 snprintf + 一次入队，绝不碰磁盘、绝不等待 I/O；
//   * 队列满直接丢弃并计数（Dropped()），永不阻塞调用方；
//   * UI 不注册回调，按 200ms 定时器 UiPoll() 拉取最近若干行。

namespace xglog {

enum class Level : int {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Fatal = 5,
    Off = 6
};

// L"TRACE" / L"DEBUG" / … / L"OFF"
const wchar_t* LevelName(Level lv);
const char* LevelTag(Level lv);   // "T" / "D" / "I" / "W" / "E" / "F"

struct Config {
    // 控制台与调试器输出的最低级别
    Level level = Level::Info;
    // 写文件的最低级别（通常比控制台更低，便于事后排查）
    Level fileLevel = Level::Debug;

    bool console = true;         // 附加到控制台时输出（彩色）
    bool debugger = false;       // 有调试器时走 OutputDebugStringA
    bool file = true;            // 写文件

    // 日志目录，空 = exe 同级 logs/
    std::wstring dir;
    std::wstring baseName = L"xglog";

    uint64_t maxFileBytes = 8ull << 20;   // 单文件上限，超过即轮转
    uint32_t maxFiles = 8;                // 轮转保留数量（不含当前文件）

    uint32_t queueCapacity = 2048;        // 环形队列条数（arena = 条数 × messageMax）
    uint32_t messageMax = 512;            // 单条日志最大字节（含结尾 0）
    uint32_t uiCapacity = 256;            // 供 UiPoll 的环形缓冲条数

    uint32_t flushIntervalMs = 200;       // 空闲时最长多久刷一次盘
    bool async = true;                    // false = 调用线程直接写（排障用）
    bool color = true;                    // 控制台 ANSI 颜色
};

// 初始化。重复调用会被忽略（返回 true）。未初始化直接打日志时会用默认配置懒初始化。
bool Init(const Config& cfg);

// 停止写线程并刷盘；之后再打日志会懒初始化一份默认配置。
void Shutdown();

void Flush();                 // 同步刷盘（Fatal 自动调用）
void SetLevel(Level lv);      // 运行时调控制台级别
Level GetLevel();

// 队列满被丢弃的条数（诊断用，不清零）
uint64_t Dropped();

// 核心入口：file/line 仅接受静态字面量（宏传入 __FILE__ / __LINE__）
void Logf(Level lv, const char* file, int line, const char* fmt, ...);
void VLogf(Level lv, const char* file, int line, const char* fmt, va_list ap);

// ---- UI 轮询 ----
struct Record {
    uint64_t seq = 0;
    int64_t timeMs = 0;       // 本地时间（自 1970 起的毫秒）
    Level level = Level::Info;
    uint32_t tid = 0;
    std::string text;         // 形如 "download.cpp:42 open failed: 2"
};

// 取一条尚未消费的记录；返回 false 表示没有新行。
// 缓冲被覆盖时自动跳到最新，不会让 UI 追着补历史。
bool UiPoll(Record& out);

} // namespace xglog

#define XGLOG_TRACE(...) ::xglog::Logf(::xglog::Level::Trace, __FILE__, __LINE__, __VA_ARGS__)
#define XGLOG_DEBUG(...) ::xglog::Logf(::xglog::Level::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define XGLOG_INFO(...)  ::xglog::Logf(::xglog::Level::Info,  __FILE__, __LINE__, __VA_ARGS__)
#define XGLOG_WARN(...)  ::xglog::Logf(::xglog::Level::Warn,  __FILE__, __LINE__, __VA_ARGS__)
#define XGLOG_ERROR(...) ::xglog::Logf(::xglog::Level::Error, __FILE__, __LINE__, __VA_ARGS__)
#define XGLOG_FATAL(...) ::xglog::Logf(::xglog::Level::Fatal, __FILE__, __LINE__, __VA_ARGS__)
