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

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

// XGTHREAD —— 线程池 + 主线程调度队列（Windows / C++17 / 静态库 / Apache-2.0）
//
// 两件互补的事：
//   * Pool：后台干活。Submit 一个任务，要结果就用返回的 future；不要结果就 fire-and-forget。
//   * MainQueue：把任务投回"拥有某个对象的那条线程"（UI、渲染、主循环）。
//     谁创建谁消费：Post 是线程安全的，执行只发生在调用 RunPending 的线程上。
// 不做优先级/取消/任务偷窃——需要那些语义时应该用专门的调度器，而不是在这里加开关。

namespace xgthread {

// 物理核数（含 HT 的逻辑核数）；读失败返回 4
uint32_t HardwareThreads();

class Pool {
public:
    explicit Pool(uint32_t threads = 0);   // 0 = HardwareThreads()
    ~Pool();
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    uint32_t Threads() const;

    // fire-and-forget；返回 false 表示池已关闭
    bool Post(std::function<void()> fn);

    // 要返回值：future 里拿；任务抛异常时 future 会 rethrow
    template <class F, class R = std::invoke_result_t<std::decay_t<F>>>
    std::future<R> Submit(F&& f) {
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> fut = task->get_future();
        if (!Post([task] { (*task)(); })) throw std::runtime_error("xgthread::Pool is shut down");
        return fut;
    }

    uint32_t Pending() const;              // 排队未开始的任务数
    bool WaitIdle(uint32_t timeoutMs);     // 队列空且无执行中任务
    void Shutdown(bool drain = true);      // drain=true 先跑完队列；之后 Submit 返回 false

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 主线程调度：任意线程 Post，拥有该队列的线程周期性 RunPending。
//
//   // UI 线程每帧：
//   queue.RunPending();          // 或限个数：RunPending(16)
//   // 任意工作线程：
//   queue.Post([this] { label->SetText(L"done"); });
class MainQueue {
public:
    MainQueue() = default;
    MainQueue(const MainQueue&) = delete;
    MainQueue& operator=(const MainQueue&) = delete;

    void Post(std::function<void()> fn) {
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(std::move(fn));
    }

    // 返回实际执行的任务数；执行期间不持锁，任务里可以继续 Post
    size_t RunPending(size_t maxTasks = static_cast<size_t>(-1)) {
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard<std::mutex> lk(mu_);
            size_t n = maxTasks < q_.size() ? maxTasks : q_.size();
            batch.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                batch.push_back(std::move(q_.front()));
                q_.pop_front();
            }
        }
        for (auto& fn : batch)
            if (fn) fn();
        return batch.size();
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return q_.size();
    }

    void Clear() {
        std::lock_guard<std::mutex> lk(mu_);
        q_.clear();
    }

private:
    mutable std::mutex mu_;
    std::deque<std::function<void()>> q_;
};

} // namespace xgthread
