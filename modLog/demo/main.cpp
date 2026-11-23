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

#include <xglog/xglog.h>

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    xglog::Config cfg;
    cfg.dir = L"demo-logs";
    cfg.baseName = L"xglog_demo";
    cfg.level = xglog::Level::Debug;
    cfg.fileLevel = xglog::Level::Trace;
    cfg.maxFileBytes = 256 * 1024;   // 故意设小，观察轮转
    cfg.maxFiles = 4;
    cfg.queueCapacity = 8192;   // 8192 × 512B ≈ 4MB，压测峰值也不丢
    cfg.uiCapacity = 64;
    cfg.debugger = true;
    xglog::Init(cfg);

    XGLOG_INFO("xglog demo 启动，参数个数 %d", argc - 1);
    XGLOG_WARN("这是一条警告，%s", argc > 1 ? argv[1] : "无额外参数");

    // 1) 多线程压测：记录调用线程耗时，确认不阻塞
    const int threads = 4;
    const int perThread = 2000;
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([t, perThread] {
            for (int i = 0; i < perThread; ++i)
                XGLOG_DEBUG("thread %d line %d value=%.3f", t, i, i * 0.5);
        });
    }
    for (auto& th : pool) th.join();
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    XGLOG_INFO("压测完成：%d 条 / %.1f ms（%.1f 万条每秒），丢弃 %llu 条",
               threads * perThread, ms,
               (threads * perThread) / (ms / 1000.0) / 10000.0,
               static_cast<unsigned long long>(xglog::Dropped()));

    // 2) 文件轮转：写够 4 个文件以上
    for (int i = 0; i < 1200; ++i) XGLOG_TRACE("rotate check %d 0123456789012345678901234567890123456789", i);

    XGLOG_ERROR("这是错误级日志，会立刻刷盘");

    // 3) UI 轮询：模拟 200ms 定时器拉取
    xglog::Flush();
    xglog::Record r;
    int polled = 0;
    while (xglog::UiPoll(r)) {
        if (polled < 3) printf("[ui] #%llu %s\n", static_cast<unsigned long long>(r.seq), r.text.c_str());
        ++polled;
    }
    printf("[ui] 一次轮询拿到 %d 条\n", polled);

    xglog::Shutdown();
    printf("done, dropped=%llu\n", static_cast<unsigned long long>(xglog::Dropped()));
    return 0;
}
