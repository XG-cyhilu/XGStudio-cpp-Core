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
#include <vector>

namespace xgdl {

enum class Priority { High = 0, Normal = 1, Low = 2 };

enum class TaskState {
    Queued,
    Downloading,
    Verifying,
    Completed,
    Failed,
    Cancelled
};

struct Config {
    // 小文件并发数（每个线程同时只持有 1 个任务）
    int smallThreads = 16;
    // 大文件分块并发数
    int largeThreads = 4;
    // 超过该字节数走分块路径
    uint64_t largeThreshold = 1ull << 20;
    // 分块大小
    uint64_t chunkSize = 8ull << 20;
    // 失败重试次数与退避
    int maxRetries = 3;
    int retryDelayMs = 1000;
    // 底层 CURLM 的总连接数与单主机连接数上限
    int maxConnections = 48;
    int maxHostConnections = 16;
    // 接收缓冲（每个任务一条环形缓冲，回调写满就暂停该 easy handle）
    uint64_t ringBytes = 1ull << 20;
    // 写盘线程数
    int writeThreads = 2;
    // 单次请求超时（秒），0 表示不设
    long connectTimeoutSec = 15;
    long idleTimeoutSec = 30;
};

struct Progress {
    uint64_t bytesTotal = 0;   // 已入队任务的预期总字节（未知大小取 0，补齐后累加）
    uint64_t bytesDone = 0;
    double bytesPerSec = 0;    // 两次 Snapshot 之间的平均速度
    uint32_t active = 0;       // 正在下载的任务数
    uint32_t queued = 0;
    uint32_t completed = 0;
    uint32_t failed = 0;
};

enum class EventKind { Completed, Failed };

struct Event {
    uint32_t id = 0;
    EventKind kind = EventKind::Completed;
    std::wstring path;
    long httpStatus = 0;
    std::wstring message;   // 失败原因；成功时为空
};

struct TaskInfo {
    uint32_t id = 0;
    std::wstring url;
    std::wstring path;
    uint64_t sizeTotal = 0;
    uint64_t sizeDone = 0;
    TaskState state = TaskState::Queued;
    Priority priority = Priority::Normal;
    int attempt = 0;
    bool large = false;
};

class Manager {
public:
    explicit Manager(const Config& cfg = Config{});
    ~Manager();
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    // 入队。返回任务 id，参数非法（空 URL / 空路径 / 重复路径）返回 0。
    // expectBytes 为 0 表示未知，运行中以 Content-Length 补齐。
    // expectSha1 为空表示不校验；否则校验失败会删文件并重下。
    uint32_t Add(const std::wstring& url,
                 const std::wstring& path,
                 uint64_t expectBytes = 0,
                 const std::wstring& expectSha1 = L"",
                 Priority priority = Priority::Normal);

    void Start();
    void Pause();    // 只停止派发新任务，已在跑的继续
    void Resume();
    void Cancel(uint32_t id);
    void CancelAll();

    // UI 每帧/每 200ms 调用一次，内部按调用间隔算速度。
    Progress Snapshot();

    // UI 轮询完成/失败事件。返回 false 表示队列已空。
    bool PollEvent(Event& out);

    bool QueryTask(uint32_t id, TaskInfo& out) const;

    // 阻塞直到没有排队与在途任务（测试与 CLI 用）。
    void WaitIdle();

    // 停止所有线程并清理。析构会自动调用。
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xgdl
