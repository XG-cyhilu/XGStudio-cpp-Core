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
struct curl_slist;

namespace xgdl {
namespace detail {

// 落盘目标。网络线程只往里塞数据块，写盘线程按队列顺序写到 baseOffset 之后。
struct Sink {
    std::wstring path;
    uint64_t baseOffset = 0;
    bool append = false;        // true = 以 "ab" 打开（断点续传接着写），否则 "wb"
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
    // 写盘线程真正关掉文件（或判定失败）后置位：调用方要等它才能改名/合并，
    // 否则会撞 Windows 的共享冲突（ERROR_SHARING_VIOLATION）
    std::atomic<bool> closed{ false };
};

using SinkPtr = std::shared_ptr<Sink>;

struct Job {
    CURL* easy = nullptr;
    SinkPtr sink;
    std::wstring url;
    std::string urlUtf8;
    std::vector<std::pair<std::string, std::string>> headers;   // 请求头（已 UTF-8 化）
    curl_slist* hdrList = nullptr;                              // 与 headers 对应，句柄销毁时释放
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
                  uint64_t rangeStart = 0, uint64_t rangeEnd = 0,
                  const std::vector<std::pair<std::wstring, std::wstring>>& headers = {});
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
    // curl_multi_* 不是线程安全的：加句柄、perform、info_read、清理都要串行
    std::mutex curlMu_;
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

// 文件级摘要（十六进制小写）。失败时返回 false 并给出错误描述（UTF-8）。
bool HashFile(HashAlg alg, const std::wstring& path, std::wstring& hex, std::string& err);
bool Sha1File(const std::wstring& path, std::wstring& hex, std::string& err);

// 文件大小；不存在返回 false
bool FileSize(const std::wstring& path, uint64_t& bytes);

// 覆盖式改名（REPLACE_EXISTING | WRITE_THROUGH）；用于 .part → 正式文件
bool MoveReplace(const std::wstring& src, const std::wstring& dst, std::string& err);

// 设置文件修改时间（毫秒，自 1970）
bool SetFileMtime(const std::wstring& path, int64_t ms);

std::string Utf8(const std::wstring& s);
std::wstring Widen(const std::string& s);

// 递归建目录（写盘前确保父目录存在）。
bool EnsureParentDir(const std::wstring& filePath, std::string& err);

} // namespace detail
} // namespace xgdl
