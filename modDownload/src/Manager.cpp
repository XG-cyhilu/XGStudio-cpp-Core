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

#include "internal.h"


#include <algorithm>
#include <chrono>

namespace xgdl {

namespace {

using Clock = std::chrono::steady_clock;

uint64_t NowMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now().time_since_epoch()).count();
}

std::wstring Lower(std::wstring s) {
    for (auto& c : s) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
    return s;
}

} // namespace

struct Entry {
    uint32_t id = 0;
    std::wstring url, path, sha1;
    uint64_t expect = 0;
    Priority prio = Priority::Normal;
    TaskState state = TaskState::Queued;
    int attempt = 0;
    bool large = false;
    uint64_t total = 0;

    std::atomic<bool> cancel{ false };
    std::atomic<bool> rangeUnsupported{ false };
    std::atomic<uint64_t> finished{ 0 };   // 已完成分块的字节数
    std::atomic<int> pending{ 0 };         // 未完成的分块数
    std::mutex mu;                          // 终态切换互斥，防止并发重复计重试
};
using EntryPtr = std::shared_ptr<Entry>;

struct Work {
    EntryPtr e;
    int chunk = -1;
    uint64_t start = 0, end = 0;
};

struct Manager::Impl {
    explicit Impl(const Config& c) : cfg(c), engine(new detail::Engine(c)) {}
    ~Impl() { Shutdown(); }

    Config cfg;
    std::unique_ptr<detail::Engine> engine;

    std::mutex qmu;
    std::condition_variable qcv;
    std::deque<EntryPtr> pending;
    std::deque<Work> smallQ, largeQ;

    std::atomic<bool> running{ false };
    std::atomic<bool> paused{ false };

    std::mutex listMu;
    std::vector<EntryPtr> entries;

    std::mutex evMu;
    std::deque<Event> events;

    std::mutex actMu;
    std::vector<std::pair<EntryPtr, detail::JobPtr>> active;

    std::mutex spMu;
    uint64_t lastBytes = 0;
    uint64_t lastTick = 0;
    double speed = 0;

    std::thread dispatcher;
    std::vector<std::thread> smallPool, largePool;
    std::atomic<uint32_t> nextId{ 1 };

    // ---- 生命周期 ----
    void Start() {
        if (running.exchange(true)) return;
        dispatcher = std::thread([this] { DispatchLoop(); });
        for (int i = 0; i < std::max(1, cfg.smallThreads); ++i)
            smallPool.emplace_back([this] { SmallLoop(); });
        for (int i = 0; i < std::max(1, cfg.largeThreads); ++i)
            largePool.emplace_back([this] { LargeLoop(); });
    }

    void Shutdown() {
        if (!running.exchange(false)) {
            if (engine) engine->Shutdown();
            return;
        }
        qcv.notify_all();
        if (dispatcher.joinable()) dispatcher.join();
        for (auto& t : smallPool) if (t.joinable()) t.join();
        for (auto& t : largePool) if (t.joinable()) t.join();
        smallPool.clear();
        largePool.clear();
        engine->Shutdown();
        engine.reset();
    }

    EntryPtr Find(uint32_t id) {
        std::lock_guard<std::mutex> lk(listMu);
        for (auto& e : entries) if (e->id == id) return e;
        return nullptr;
    }

    void PushEvent(const EntryPtr& e, EventKind kind, long status, const std::wstring& msg) {
        Event ev;
        ev.id = e->id;
        ev.kind = kind;
        ev.path = e->path;
        ev.httpStatus = status;
        ev.message = msg;
        std::lock_guard<std::mutex> lk(evMu);
        events.push_back(std::move(ev));
    }

    // ---- 派发 ----
    void DispatchLoop() {
        while (running.load()) {
            EntryPtr e;
            {
                std::unique_lock<std::mutex> lk(qmu);
                if (paused.load() || pending.empty()) {
                    qcv.wait_for(lk, std::chrono::milliseconds(5));
                }
                if (!running.load()) break;
                if (paused.load() || pending.empty()) continue;
                auto best = pending.begin();
                for (auto it = pending.begin(); it != pending.end(); ++it)
                    if ((int)(*it)->prio < (int)(*best)->prio) best = it;
                e = *best;
                pending.erase(best);
            }
            Plan(e);
        }
    }

    void Plan(const EntryPtr& e) {
        if (e->cancel.load()) { FinishCancelled(e); return; }
        bool chunkable = (e->expect > cfg.largeThreshold) && !e->rangeUnsupported.load();
        if (!chunkable) {
            e->large = false;
            e->total = e->expect;
            Work w; w.e = e; w.chunk = -1;
            {
                std::lock_guard<std::mutex> lk(qmu);
                smallQ.push_back(w);
            }
            qcv.notify_all();
            return;
        }

        e->large = true;
        e->total = e->expect;
        uint64_t chunkSize = cfg.chunkSize ? cfg.chunkSize : (8ull << 20);
        int n = (int)((e->expect + chunkSize - 1) / chunkSize);
        if (n <= 0) n = 1;
        e->pending.store(n);
        e->finished.store(0);

        for (int i = 0; i < n; ++i) {
            Work w;
            w.e = e;
            w.chunk = i;
            w.start = (uint64_t)i * chunkSize;
            w.end = std::min(w.start + chunkSize - 1, e->expect - 1);
            std::lock_guard<std::mutex> lk(qmu);
            largeQ.push_back(w);
        }
        qcv.notify_all();
    }

    // ---- 工作线程 ----
    void SmallLoop() {
        while (running.load()) {
            Work w;
            {
                std::unique_lock<std::mutex> lk(qmu);
                if (smallQ.empty()) qcv.wait_for(lk, std::chrono::milliseconds(5));
                if (!running.load()) break;
                if (smallQ.empty()) continue;
                w = smallQ.front();
                smallQ.pop_front();
            }
            RunSmall(w.e);
        }
    }

    void LargeLoop() {
        while (running.load()) {
            Work w;
            {
                std::unique_lock<std::mutex> lk(qmu);
                if (largeQ.empty()) qcv.wait_for(lk, std::chrono::milliseconds(5));
                if (!running.load()) break;
                if (largeQ.empty()) continue;
                w = largeQ.front();
                largeQ.pop_front();
            }
            RunChunk(w);
        }
    }

    // 等待一个 job 结束（可被 cancel / shutdown 打断）
    bool Await(const detail::JobPtr& job) {
        std::unique_lock<std::mutex> lk(job->mu);
        while (!job->done) {
            job->cvDone.wait_for(lk, std::chrono::milliseconds(100));
            if (!running.load()) return false;
        }
        return true;
    }

    void Track(const EntryPtr& e, const detail::JobPtr& job) {
        std::lock_guard<std::mutex> lk(actMu);
        active.emplace_back(e, job);
    }

    void Untrack(const detail::JobPtr& job) {
        std::lock_guard<std::mutex> lk(actMu);
        for (auto it = active.begin(); it != active.end(); ++it) {
            if (it->second.get() == job.get()) { active.erase(it); break; }
        }
    }

    void RunSmall(const EntryPtr& e) {
        if (e->cancel.load()) { FinishCancelled(e); return; }
        e->state = TaskState::Downloading;

        auto sink = std::make_shared<detail::Sink>();
        sink->path = e->path;
        sink->maxQueueBytes = cfg.ringBytes;
        std::string err;
        if (!detail::EnsureParentDir(e->path, err)) { Fail(e, 0, detail::Widen(err)); return; }

        auto job = engine->Submit(e->url, sink);
        Track(e, job);
        Await(job);
        Untrack(job);

        if (e->cancel.load()) { CleanupFile(e->path); FinishCancelled(e); return; }
        if (!job->ok) { Fail(e, job->httpStatus, detail::Widen(job->error)); return; }
        if (sink->failed) { CleanupFile(e->path); Fail(e, job->httpStatus, L"disk write failed"); return; }

        e->finished.store(job->bytesDone.load());
        if (job->contentLength) e->total = job->contentLength;

        if (!Verify(e)) return;
        Complete(e);
    }

    void RunChunk(const Work& w) {
        EntryPtr e = w.e;
        if (!e) return;
        if (e->cancel.load()) { FinishCancelled(e); return; }
        if (e->rangeUnsupported.load() && w.chunk >= 0) return;   // 有人已判定不支持 Range

        e->state = TaskState::Downloading;
        std::wstring part = e->path + L".part" + std::to_wstring(w.chunk);

        auto sink = std::make_shared<detail::Sink>();
        sink->path = part;
        sink->maxQueueBytes = cfg.ringBytes;
        std::string err;
        if (!detail::EnsureParentDir(part, err)) { Fail(e, 0, detail::Widen(err)); return; }

        auto job = engine->Submit(e->url, sink, w.start, w.end);
        Track(e, job);
        Await(job);
        Untrack(job);

        if (e->cancel.load()) { CleanupFile(part); FinishCancelled(e); return; }

        if (job->ok && w.chunk > 0 && job->httpStatus == 200) {
            // 服务器忽略了 Range，回退到单流下载
            e->rangeUnsupported.store(true);
            CleanupFile(part);
            CancelSiblings(e);
            RequeueSingle(e);
            return;
        }

        if (!job->ok) { CleanupFile(part); Fail(e, job->httpStatus, detail::Widen(job->error)); return; }
        if (sink->failed) { CleanupFile(part); Fail(e, job->httpStatus, L"disk write failed"); return; }

        uint64_t got = job->bytesDone.load();
        e->finished.fetch_add(got);
        if (e->pending.fetch_sub(1) != 1) return;   // 还有分块没完

        // 最后一块：合并且校验
        e->state = TaskState::Verifying;
        if (!MergeChunks(e)) { CleanupParts(e); Fail(e, 0, L"merge failed"); return; }
        CleanupParts(e);
        if (!Verify(e)) return;
        Complete(e);
    }

    void CancelSiblings(const EntryPtr& e) {
        std::vector<detail::JobPtr> jobs;
        {
            std::lock_guard<std::mutex> lk(actMu);
            for (auto& p : active) if (p.first.get() == e.get()) jobs.push_back(p.second);
        }
        for (auto& j : jobs) engine->Cancel(j);
    }

    void RequeueSingle(const EntryPtr& e) {
        if (!running.load()) return;
        {
            std::lock_guard<std::mutex> lk(qmu);
            Work w; w.e = e; w.chunk = -1;
            smallQ.push_back(w);
        }
        qcv.notify_all();
    }

    bool MergeChunks(const EntryPtr& e) {
        std::FILE* out = _wfopen(e->path.c_str(), L"wb");
        if (!out) return false;
        std::vector<uint8_t> buf(1 << 20);
        int n = (int)e->pending.load();
        // pending 已归零，用 total 反推块数
        uint64_t cs = cfg.chunkSize ? cfg.chunkSize : (8ull << 20);
        n = (int)((e->total + cs - 1) / cs);
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            std::wstring part = e->path + L".part" + std::to_wstring(i);
            std::FILE* in = _wfopen(part.c_str(), L"rb");
            if (!in) { ok = false; break; }
            size_t r = 0;
            while ((r = std::fread(buf.data(), 1, buf.size(), in)) > 0) {
                if (std::fwrite(buf.data(), 1, r, out) != r) { ok = false; break; }
            }
            std::fclose(in);
            if (!ok) break;
        }
        std::fflush(out);
        std::fclose(out);
        return ok;
    }

    void CleanupParts(const EntryPtr& e) {
        uint64_t cs = cfg.chunkSize ? cfg.chunkSize : (8ull << 20);
        int n = e->total ? (int)((e->total + cs - 1) / cs) : 0;
        for (int i = 0; i < n; ++i) {
            std::wstring part = e->path + L".part" + std::to_wstring(i);
            _wremove(part.c_str());
        }
    }

    void CleanupFile(const std::wstring& p) { _wremove(p.c_str()); }

    bool Verify(const EntryPtr& e) {
        if (e->sha1.empty()) return true;
        e->state = TaskState::Verifying;
        std::wstring hex;
        std::string err;
        if (!detail::Sha1File(e->path, hex, err)) {
            CleanupFile(e->path);
            Fail(e, 0, L"sha1 read failed: " + detail::Widen(err));
            return false;
        }
        if (Lower(hex) != Lower(e->sha1)) {
            CleanupFile(e->path);
            Fail(e, 0, L"sha1 mismatch: got " + hex);
            return false;
        }
        return true;
    }

    void Complete(const EntryPtr& e) {
        {
            std::lock_guard<std::mutex> lk(e->mu);
            if (e->state == TaskState::Completed || e->state == TaskState::Failed ||
                e->state == TaskState::Cancelled) return;
            e->state = TaskState::Completed;
        }
        PushEvent(e, EventKind::Completed, 200, L"");
    }

    void FinishCancelled(const EntryPtr& e) {
        {
            std::lock_guard<std::mutex> lk(e->mu);
            if (e->state == TaskState::Cancelled || e->state == TaskState::Completed) return;
            e->state = TaskState::Cancelled;
        }
        PushEvent(e, EventKind::Failed, 0, L"cancelled");
    }

    void Fail(const EntryPtr& e, long status, const std::wstring& msg) {
        if (e->cancel.load()) { FinishCancelled(e); return; }

        bool retry = false;
        {
            std::lock_guard<std::mutex> lk(e->mu);
            if (e->state == TaskState::Failed || e->state == TaskState::Completed ||
                e->state == TaskState::Cancelled) return;
            e->attempt++;
            if (e->attempt <= cfg.maxRetries) {
                e->state = TaskState::Queued;
                e->finished.store(0);
                e->pending.store(0);
                e->rangeUnsupported.store(false);
                retry = true;
            } else {
                e->state = TaskState::Failed;
            }
        }

        // 先让同任务的其他分块停下，避免它们各自再走一次重试逻辑
        CancelSiblings(e);

        if (retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.retryDelayMs > 0 ? cfg.retryDelayMs : 1));
            if (!running.load()) return;
            {
                std::lock_guard<std::mutex> lk(qmu);
                pending.push_back(e);
            }
            qcv.notify_all();
            return;
        }
        PushEvent(e, EventKind::Failed, status, msg);
    }

    // ---- 对外 ----
    Progress Snapshot() {
        Progress p;
        uint64_t done = 0, total = 0;
        uint32_t activeCount = 0, queued = 0, completed = 0, failed = 0;
        {
            std::lock_guard<std::mutex> lk(listMu);
            for (auto& e : entries) {
                total += e->total ? e->total : e->expect;
                done += e->finished.load();
                switch (e->state) {
                case TaskState::Queued: ++queued; break;
                case TaskState::Downloading:
                case TaskState::Verifying: ++activeCount; break;
                case TaskState::Completed: ++completed; break;
                case TaskState::Failed: ++failed; break;
                default: break;
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(actMu);
            for (auto& a : active) done += a.second->bytesDone.load();
        }
        p.bytesTotal = total;
        p.bytesDone = done;
        p.active = activeCount;
        p.queued = queued;
        p.completed = completed;
        p.failed = failed;

        uint64_t now = NowMs();
        std::lock_guard<std::mutex> lk(spMu);
        if (lastTick && now > lastTick) {
            double dt = (double)(now - lastTick) / 1000.0;
            double db = (double)(done > lastBytes ? done - lastBytes : 0);
            double inst = db / dt;
            speed = speed * 0.7 + inst * 0.3;   // 轻量平滑，UI 不跳
        }
        lastBytes = done;
        lastTick = now;
        p.bytesPerSec = speed;
        return p;
    }

    bool PollEvent(Event& out) {
        std::lock_guard<std::mutex> lk(evMu);
        if (events.empty()) return false;
        out = std::move(events.front());
        events.pop_front();
        return true;
    }

    bool Query(uint32_t id, TaskInfo& out) {
        auto e = Find(id);
        if (!e) return false;
        out.id = e->id;
        out.url = e->url;
        out.path = e->path;
        out.sizeTotal = e->total ? e->total : e->expect;
        out.sizeDone = e->finished.load();
        out.state = e->state;
        out.priority = e->prio;
        out.attempt = e->attempt;
        out.large = e->large;
        return true;
    }

    void WaitIdle() {
        while (running.load()) {
            bool busy = false;
            {
                std::lock_guard<std::mutex> lk(qmu);
                busy = !pending.empty() || !smallQ.empty() || !largeQ.empty();
            }
            {
                std::lock_guard<std::mutex> lk(actMu);
                if (!active.empty()) busy = true;
            }
            if (!busy) {
                std::lock_guard<std::mutex> lk(listMu);
                for (auto& e : entries)
                    if (e->state == TaskState::Downloading || e->state == TaskState::Verifying) { busy = true; break; }
            }
            if (!busy) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
};

// ---------------- Manager 对外实现 ----------------

Manager::Manager(const Config& cfg) : impl_(new Impl(cfg)) {}

Manager::~Manager() = default;

uint32_t Manager::Add(const std::wstring& url, const std::wstring& path,
                      uint64_t expectBytes, const std::wstring& expectSha1,
                      Priority priority) {
    if (url.empty() || path.empty()) return 0;
    {
        std::lock_guard<std::mutex> lk(impl_->listMu);
        for (auto& e : impl_->entries) if (e->path == path) return 0;
    }
    auto e = std::make_shared<Entry>();
    e->id = impl_->nextId.fetch_add(1);
    e->url = url;
    e->path = path;
    e->expect = expectBytes;
    e->sha1 = expectSha1;
    e->prio = priority;
    e->total = expectBytes;
    {
        std::lock_guard<std::mutex> lk(impl_->listMu);
        impl_->entries.push_back(e);
    }
    {
        std::lock_guard<std::mutex> lk(impl_->qmu);
        impl_->pending.push_back(e);
    }
    impl_->qcv.notify_all();
    return e->id;
}

void Manager::Start() { impl_->Start(); }

void Manager::Pause() { impl_->paused.store(true); }

void Manager::Resume() {
    impl_->paused.store(false);
    impl_->qcv.notify_all();
}

void Manager::Cancel(uint32_t id) {
    auto e = impl_->Find(id);
    if (!e) return;
    e->cancel.store(true);
    impl_->CancelSiblings(e);
    {
        std::lock_guard<std::mutex> lk(impl_->qmu);
        for (auto it = impl_->pending.begin(); it != impl_->pending.end(); ++it)
            if (it->get() == e.get()) { impl_->pending.erase(it); break; }
        for (auto it = impl_->smallQ.begin(); it != impl_->smallQ.end(); ++it)
            if (it->e.get() == e.get()) { impl_->smallQ.erase(it); break; }
        for (auto it = impl_->largeQ.begin(); it != impl_->largeQ.end(); ++it)
            if (it->e.get() == e.get()) { impl_->largeQ.erase(it); break; }
    }
    impl_->qcv.notify_all();
}

void Manager::CancelAll() {
    std::vector<uint32_t> ids;
    {
        std::lock_guard<std::mutex> lk(impl_->listMu);
        for (auto& e : impl_->entries)
            if (e->state == TaskState::Queued || e->state == TaskState::Downloading) ids.push_back(e->id);
    }
    for (auto id : ids) Cancel(id);
}

Progress Manager::Snapshot() { return impl_->Snapshot(); }

bool Manager::PollEvent(Event& out) { return impl_->PollEvent(out); }

bool Manager::QueryTask(uint32_t id, TaskInfo& out) const { return impl_->Query(id, out); }

void Manager::WaitIdle() { impl_->WaitIdle(); }

void Manager::Shutdown() { impl_->Shutdown(); }

} // namespace xgdl
