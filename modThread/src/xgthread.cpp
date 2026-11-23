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

#include "xgthread/xgthread.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>

namespace xgthread {

uint32_t HardwareThreads() {
    DWORD_PTR mask = 0;
    DWORD_PTR sys = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &mask, &sys)) return 4;
    uint32_t n = 0;
    for (DWORD_PTR m = mask; m; m >>= 1) n += static_cast<uint32_t>(m & 1);
    return n ? n : 4;
}

struct Pool::Impl {
    std::vector<std::thread> workers;
    mutable std::mutex mu;
    std::condition_variable cv;      // 有任务 / 退出
    std::condition_variable idleCv;  // 空闲
    std::deque<std::function<void()>> queue;
    uint32_t threads = 0;
    uint32_t active = 0;
    bool stopped = false;

    void Work() {
        while (true) {
            std::function<void()> fn;
            {
                std::unique_lock<std::mutex> lk(mu);
                cv.wait(lk, [this] { return stopped || !queue.empty(); });
                if (stopped && queue.empty()) return;
                fn = std::move(queue.front());
                queue.pop_front();
                ++active;
            }
            try {
                fn();
            } catch (...) {
                // 后台任务抛出：不杀线程，也不静默——交给任务自己的 future/捕获处理
            }
            {
                std::lock_guard<std::mutex> lk(mu);
                --active;
                if (active == 0 && queue.empty()) idleCv.notify_all();
            }
        }
    }

    void Stop(bool drain) {
        {
            std::unique_lock<std::mutex> lk(mu);
            if (drain) {
                cv.wait_for(lk, std::chrono::seconds(60), [this] { return queue.empty() && active == 0; });
            } else {
                queue.clear();
            }
            stopped = true;
        }
        cv.notify_all();
        for (auto& t : workers)
            if (t.joinable()) t.join();
        workers.clear();
    }
};

Pool::Pool(uint32_t threads) : impl_(new Impl) {
    impl_->threads = threads ? threads : HardwareThreads();
    impl_->workers.reserve(impl_->threads);
    for (uint32_t i = 0; i < impl_->threads; ++i) impl_->workers.emplace_back([this] { impl_->Work(); });
}

Pool::~Pool() { impl_->Stop(true); }

uint32_t Pool::Threads() const { return impl_->threads; }

bool Pool::Post(std::function<void()> fn) {
    if (!fn) return false;
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        if (impl_->stopped) return false;
        impl_->queue.push_back(std::move(fn));
    }
    impl_->cv.notify_one();
    return true;
}

uint32_t Pool::Pending() const {
    std::lock_guard<std::mutex> lk(impl_->mu);
    return static_cast<uint32_t>(impl_->queue.size());
}

bool Pool::WaitIdle(uint32_t timeoutMs) {
    std::unique_lock<std::mutex> lk(impl_->mu);
    return impl_->idleCv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                                  [this] { return impl_->queue.empty() && impl_->active == 0; });
}

void Pool::Shutdown(bool drain) { impl_->Stop(drain); }

} // namespace xgthread
