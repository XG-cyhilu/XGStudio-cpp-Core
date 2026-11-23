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

#include <xgthread/xgthread.h>

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <numeric>
#include <vector>

int main() {
    printf("[1] hardware threads = %u\n", xgthread::HardwareThreads());

    xgthread::Pool pool;
    printf("[1] pool threads = %u\n", pool.Threads());

    // 2) 带返回值：并行折半求和
    std::vector<std::future<int64_t>> futs;
    const int kChunks = 64, kPer = 1000;
    for (int i = 0; i < kChunks; ++i) {
        futs.push_back(pool.Submit([i, kPer] {
            int64_t s = 0;
            for (int k = i * kPer; k < (i + 1) * kPer; ++k) s += k;
            return s;
        }));
    }
    int64_t sum = 0;
    for (auto& f : futs) sum += f.get();
    printf("[2] sum=%lld expected=%lld\n", static_cast<long long>(sum),
           static_cast<long long>(static_cast<int64_t>(kChunks * kPer) * (kChunks * kPer - 1) / 2));

    // 3) fire-and-forget 计数 + WaitIdle
    std::atomic<int> hits{ 0 };
    for (int i = 0; i < 500; ++i) pool.Post([&hits] { hits.fetch_add(1, std::memory_order_relaxed); });
    bool idle = pool.WaitIdle(30'000);
    printf("[3] idle=%d hits=%d pending=%u\n", (int)idle, hits.load(), pool.Pending());

    // 4) 异常穿过 future
    auto bad = pool.Submit([]() -> int { throw std::runtime_error("boom"); });
    try {
        bad.get();
        printf("[4] unexpected: no exception\n");
    } catch (const std::exception& e) {
        printf("[4] caught: %s\n", e.what());
    }

    // 5) MainQueue：worker 投递，主线程执行（校验线程 id）
    xgthread::MainQueue q;
    DWORD mainTid = GetCurrentThreadId();
    std::atomic<int> onMain{ 0 }, wrongThread{ 0 };
    for (int i = 0; i < 20; ++i) {
        pool.Post([&q, &onMain, &wrongThread, mainTid] {
            q.Post([&onMain, &wrongThread, mainTid] {
                if (GetCurrentThreadId() == mainTid) ++onMain;
                else ++wrongThread;
            });
        });
    }
    pool.WaitIdle(10'000);
    size_t ran = 0;
    while (q.Size()) ran += q.RunPending(8);
    printf("[5] queued-run=%zu onMain=%d wrongThread=%d queueEmpty=%zu\n", ran, onMain.load(), wrongThread.load(),
           q.Size());

    // 6) 关闭后 Submit 失败
    pool.Shutdown(true);
    printf("[6] post after shutdown = %d\n", (int)pool.Post([] {}));

    // 7) 菱形：任务里再提交任务（池足够大时不死锁，这里用 future 保证有进展）
    xgthread::Pool p2(4);
    auto outer = p2.Submit([&p2] {
        std::vector<std::future<int>> inner;
        for (int i = 0; i < 4; ++i) inner.push_back(p2.Submit([i] { return i * 10; }));
        int s = 0;
        for (auto& f : inner) s += f.get();
        return s;
    });
    printf("[7] nested sum=%d\n", outer.get());
    p2.Shutdown(true);
    return 0;
}
