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

#include "xgdl/xgdl.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

typedef void CURL;
typedef void CURLM;

namespace xgdl {
namespace detail {

// 落盘目标。网络线程只往里塞数据块，写盘线程按队列顺序写到 baseOffset 之后。
struct Sink {
    std::wstring path;
    uint64_t baseOffset = 0;
    uint64_t maxQueueBytes = 1ull << 20;

    std::mutex mu;
    std::atomic_flag busy = ATOMIC_FLAG_INIT;   // 写盘线程互斥：同一 sink 同时只允许一个线程写
    std::condition_variable cv;
    std::deque<std::vector<uint8_t>> chunks;
    uint64_t queuedBytes = 0;
    uint64_t writtenBytes = 0;
    bool streamEnded = false;   // 网络侧已结束，队列排空后写线程关文件
    std::atomic<bool> failed{ false };

    std::FILE* file = nullptr;
    bool opened = false;
    int openError = 0;
};

using SinkPtr = std::shared_ptr<Sink>;

struct Job {
    CURL* easy = nullptr;
    SinkPtr sink;
    std::wstring url;
    std::string urlUtf8;
    uint64_t rangeStart = 0;
    uint64_t rangeEnd = 0;      // 0 表示不设上限

    std::atomic<uint64_t> bytesDone{ 0 };
    std::atomic<bool> cancel{ false };
    std::atomic<bool> needResume{ false };
    bool paused = false;

    std::mutex mu;
    std::condition_variable cvDone;
    bool done = false;
    bool ok = false;
    long httpStatus = 0;
    std::string error;
    uint64_t contentLength = 0;
};

using JobPtr = std::shared_ptr<Job>;

class Engine {
public:
    explicit Engine(const Config& cfg);
    ~Engine();

    JobPtr Submit(const std::wstring& url, const SinkPtr& sink,
                  uint64_t rangeStart = 0, uint64_t rangeEnd = 0);
    void Cancel(const JobPtr& job);
    void Shutdown();

private:
    void NetworkLoop();
    void WriterLoop();
    void FinishJob(JobPtr& job, int result);
    void ResumePausedJobs();
    void RegisterSink(const SinkPtr& s);
    void SignalStreamEnd(const SinkPtr& s);

    Config cfg_;
    CURLM* multi_ = nullptr;
    std::thread net_;
    std::vector<std::thread> writers_;
    std::atomic<bool> running_{ true };

    std::mutex allMu_;
    std::vector<JobPtr> all_;      // 在途 + 待移除
    std::vector<SinkPtr> sinks_;
    std::condition_variable wake_;

    std::mutex writerMu_;
    std::condition_variable writerCv_;
    bool writerStop_ = false;
};

// 文件级 SHA1（十六进制小写）。失败时返回 false 并给出错误描述（UTF-8）。
bool Sha1File(const std::wstring& path, std::wstring& hex, std::string& err);

std::string Utf8(const std::wstring& s);
std::wstring Widen(const std::string& s);

// 递归建目录（写盘前确保父目录存在）。
bool EnsureParentDir(const std::wstring& filePath, std::string& err);

} // namespace detail
} // namespace xgdl
