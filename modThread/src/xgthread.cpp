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
#include <atomic>
#include <chrono>
#include <condition_variable>

namespace xgthread {

namespace detail {

// 一份令牌状态：原子标志 + 子令牌弱引用（不拖住子令牌的生命周期）
struct CancelState {
    std::atomic<bool> cancelled{ false };
    mutable std::mutex mu;                          // 保护 kids；也供 WaitCancel 的条件变量
    mutable std::condition_variable cv;
    std::vector<std::weak_ptr<CancelState>> kids;

    void Cancel() {
        {
            std::lock_guard<std::mutex> lk(mu);
            if (cancelled.load()) return;           // 幂等
            cancelled.store(true);
        }
        cv.notify_all();
        std::vector<std::shared_ptr<CancelState>> snapshot;
        {
            std::lock_guard<std::mutex> lk(mu);
            for (auto& w : kids)
                if (auto s = w.lock()) snapshot.push_back(s);
        }
        for (auto& s : snapshot) s->Cancel();
    }
};

} // namespace detail

CancelToken::CancelToken() : st_(std::make_shared<detail::CancelState>()) {}

CancelToken::CancelToken(std::shared_ptr<detail::CancelState> st) : st_(std::move(st)) {}

void CancelToken::Cancel() {
    if (st_) st_->Cancel();
}

bool CancelToken::Cancelled() const {
    return st_ && st_->cancelled.load(std::memory_order_relaxed);
}

// 定义在 .cpp：头里内联写 throw Cancelled{} 会先解析到同名成员函数 Cancelled()，MSVC 报 C3321
void CancelToken::ThrowIfCancelled() const {
    if (Cancelled()) throw xgthread::Cancelled{};
}

bool CancelToken::WaitCancel(uint32_t timeoutMs) const {
    if (!st_) return false;
    std::unique_lock<std::mutex> lk(st_->mu);
    return st_->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                            [this] { return st_->cancelled.load(std::memory_order_relaxed); });
}

CancelToken CancelToken::Child(const CancelToken& parent) {
    auto st = std::make_shared<detail::CancelState>();
    if (parent.st_) {
        std::lock_guard<std::mutex> lk(parent.st_->mu);
        // 父已取消：子直接继承状态，不用再登记
        if (parent.st_->cancelled.load(std::memory_order_relaxed)) st->cancelled.store(true);
        else parent.st_->kids.push_back(st);
    }
    return CancelToken(std::move(st));
}

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
