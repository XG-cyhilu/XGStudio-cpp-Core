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

#include <curl/curl.h>

#include <chrono>

namespace xgdl {
namespace detail {

namespace {

std::once_flag g_initOnce;

// 接收回调：只做拷贝 + 计数，绝不在这里写盘。
// 缓冲满了就返回 PAUSE，让这个 transfer 停下来，由网络线程择机续传。
size_t WriteCb(char* data, size_t size, size_t nmemb, void* userdata) {
    Job* job = static_cast<Job*>(userdata);
    size_t n = size * nmemb;
    if (job->cancel.load(std::memory_order_relaxed)) return 0;

    SinkPtr sink = job->sink;
    {
        std::lock_guard<std::mutex> lk(sink->mu);
        if (sink->maxQueueBytes && sink->queuedBytes >= sink->maxQueueBytes) {
            job->paused = true;
            return CURL_WRITEFUNC_PAUSE;
        }
        sink->chunks.emplace_back();
        sink->chunks.back().assign(reinterpret_cast<const uint8_t*>(data),
                                   reinterpret_cast<const uint8_t*>(data) + n);
        sink->queuedBytes += n;
    }
    job->bytesDone.fetch_add(n, std::memory_order_relaxed);
    return n;
}

} // namespace

Engine::Engine(const Config& cfg) : cfg_(cfg) {
    std::call_once(g_initOnce, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    multi_ = curl_multi_init();
    if (multi_) {
        curl_multi_setopt(multi_, CURLMOPT_MAX_TOTAL_CONNECTIONS, (long)cfg_.maxConnections);
        curl_multi_setopt(multi_, CURLMOPT_MAX_HOST_CONNECTIONS, (long)cfg_.maxHostConnections);
        curl_multi_setopt(multi_, CURLMOPT_MAXCONNECTS, (long)cfg_.maxConnections);
    }
    net_ = std::thread([this] { NetworkLoop(); });

    int w = cfg_.writeThreads < 1 ? 1 : cfg_.writeThreads;
    for (int i = 0; i < w; ++i) writers_.emplace_back([this] { WriterLoop(); });
}

Engine::~Engine() { Shutdown(); }

void Engine::Shutdown() {
    if (!running_.exchange(false)) return;
    {
        std::lock_guard<std::mutex> lk(writerMu_);
        writerStop_ = true;
    }
    writerCv_.notify_all();
    if (multi_) curl_multi_wakeup(multi_);
    if (net_.joinable()) net_.join();
    for (auto& t : writers_) if (t.joinable()) t.join();

    std::vector<JobPtr> jobs;
    {
        std::lock_guard<std::mutex> lk(allMu_);
        jobs.swap(all_);
    }
    for (auto& j : jobs) {
        if (j->easy) {
            std::lock_guard<std::mutex> lk(curlMu_);
            curl_multi_remove_handle(multi_, j->easy);
            curl_easy_cleanup(j->easy);
            j->easy = nullptr;
        }
        if (j->hdrList) { curl_slist_free_all(j->hdrList); j->hdrList = nullptr; }
        std::lock_guard<std::mutex> lk(j->mu);
        j->ok = false;
        if (j->error.empty()) j->error = "shutdown";
        j->done = true;
        j->cvDone.notify_all();
    }
    {
        std::lock_guard<std::mutex> lk(writerMu_);
        for (auto& s : sinks_) {
            if (s->file) { std::fclose(s->file); s->file = nullptr; }
            s->closed.store(true);
        }
        sinks_.clear();
    }
    if (multi_) {
        std::lock_guard<std::mutex> lk(curlMu_);
        curl_multi_cleanup(multi_);
        multi_ = nullptr;
    }
}

void Engine::RegisterSink(const SinkPtr& s) {
    std::lock_guard<std::mutex> lk(writerMu_);
    sinks_.push_back(s);
    writerCv_.notify_all();
}

void Engine::SignalStreamEnd(const SinkPtr& s) {
    // 注意锁序：这里绝不能在持有 sink->mu 时去拿 writerMu，
    // 写盘线程在收尾分支是 writerMu → sink->mu 的顺序。
    {
        std::lock_guard<std::mutex> lk(s->mu);
        s->streamEnded = true;
    }
    {
        std::lock_guard<std::mutex> lk(writerMu_);
        writerCv_.notify_all();
    }
}

JobPtr Engine::Submit(const std::wstring& url, const SinkPtr& sink,
                      uint64_t rangeStart, uint64_t rangeEnd,
                      const std::vector<std::pair<std::wstring, std::wstring>>& headers) {
    JobPtr job = std::make_shared<Job>();
    job->sink = sink;
    job->url = url;
    job->urlUtf8 = Utf8(url);
    job->rangeStart = rangeStart;
    job->rangeEnd = rangeEnd;

    job->easy = curl_easy_init();
    if (!job->easy) {
        std::lock_guard<std::mutex> lk(job->mu);
        job->done = true;
        job->error = "curl_easy_init failed";
        return job;
    }

    CURL* e = job->easy;
    curl_easy_setopt(e, CURLOPT_URL, job->urlUtf8.c_str());
    curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(e, CURLOPT_WRITEDATA, job.get());
    curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(e, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(e, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(e, CURLOPT_USERAGENT, "XGDL/1.0 (Windows; +https://github.com/XG-cyhilu/Void-Craft-UI)");
    curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT, (long)cfg_.connectTimeoutSec);
    curl_easy_setopt(e, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(e, CURLOPT_LOW_SPEED_TIME, (long)cfg_.idleTimeoutSec);
    // Schannel 默认做严格吊销检查，离线/受限网络会直接 SSL connect error
    // (CRYPT_E_NO_REVOCATION_CHECK)。这里退回"尽力而为"，证书链仍然验证。
    curl_easy_setopt(e, CURLOPT_SSL_OPTIONS, (long)(CURLSSLOPT_REVOKE_BEST_EFFORT | CURLSSLOPT_NO_PARTIALCHAIN));

    if (cfg_.maxSpeedBytesPerSec) {
        curl_easy_setopt(e, CURLOPT_MAX_RECV_SPEED_LARGE,
                         (curl_off_t)cfg_.maxSpeedBytesPerSec);
    }

    // 每任务请求头：先压成 UTF-8 再交给 curl；"头名: 值" 格式，空值头可用于删除默认头
    for (auto& h : headers) {
        std::string line = Utf8(h.first) + ": " + Utf8(h.second);
        job->headers.emplace_back(Utf8(h.first), Utf8(h.second));
        job->hdrList = curl_slist_append(job->hdrList, line.c_str());
    }
    if (job->hdrList) curl_easy_setopt(e, CURLOPT_HTTPHEADER, job->hdrList);

    std::string range;
    if (rangeEnd > 0) {
        range = std::to_string(rangeStart) + "-" + std::to_string(rangeEnd);
        curl_easy_setopt(e, CURLOPT_RANGE, range.c_str());
    } else if (rangeStart > 0) {
        range = std::to_string(rangeStart) + "-";
        curl_easy_setopt(e, CURLOPT_RANGE, range.c_str());
    }

    {
        std::lock_guard<std::mutex> lk(allMu_);
        all_.push_back(job);
    }
    if (sink) RegisterSink(sink);

    CURLMcode mrc;
    {
        std::lock_guard<std::mutex> lk(curlMu_);
        mrc = multi_ ? curl_multi_add_handle(multi_, e) : CURLM_BAD_HANDLE;
    }
    if (mrc != CURLM_OK) {
        std::lock_guard<std::mutex> lk(job->mu);
        job->error = std::string("curl_multi_add_handle: ") + curl_multi_strerror(mrc);
        job->ok = false;
        job->done = true;
        job->cvDone.notify_all();
        if (job->hdrList) { curl_slist_free_all(job->hdrList); job->hdrList = nullptr; }
    }
    curl_multi_wakeup(multi_);
    return job;
}

void Engine::Cancel(const JobPtr& job) {
    job->cancel.store(true);
    if (multi_) curl_multi_wakeup(multi_);
}

void Engine::FinishJob(JobPtr& job, int result) {
    long status = 0;
    curl_off_t clen = 0;
    if (job->easy) {
        curl_easy_getinfo(job->easy, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_getinfo(job->easy, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &clen);
    }
    {
        std::lock_guard<std::mutex> lk(job->mu);
        job->httpStatus = status;
        job->contentLength = (uint64_t)(clen > 0 ? clen : 0);
        if (job->cancel.load()) {
            job->ok = false;
            job->error = "cancelled";
        } else if (result != CURLE_OK) {
            job->ok = false;
            job->error = curl_easy_strerror((CURLcode)result);
        } else if (status < 200 || status > 299) {
            job->ok = false;
            job->error = "HTTP " + std::to_string(status);
        } else {
            job->ok = true;
        }
        job->done = true;
        job->cvDone.notify_all();
    }
    if (job->sink) SignalStreamEnd(job->sink);
}

void Engine::ResumePausedJobs() {
    std::vector<JobPtr> jobs;
    {
        std::lock_guard<std::mutex> lk(allMu_);
        jobs = all_;
    }
    for (auto& j : jobs) {
        if (!j->paused || !j->easy || !j->sink) continue;
        uint64_t queued = 0;
        {
            std::lock_guard<std::mutex> lk(j->sink->mu);
            queued = j->sink->queuedBytes;
        }
        if (queued < j->sink->maxQueueBytes / 2) {
            j->paused = false;
            curl_easy_pause(j->easy, CURLPAUSE_CONT);
        }
    }
}

void Engine::NetworkLoop() {
    while (running_.load()) {
        int stillRunning = 0;
        {
            std::lock_guard<std::mutex> lk(curlMu_);
            curl_multi_perform(multi_, &stillRunning);
        }

        CURLMsg* msg = nullptr;
        int inQueue = 0;
        for (;;) {
            {
                std::lock_guard<std::mutex> lk(curlMu_);
                msg = curl_multi_info_read(multi_, &inQueue);
            }
            if (!msg) break;
            if (msg->msg != CURLMSG_DONE) continue;
            CURL* easy = msg->easy_handle;
            JobPtr job;
            {
                std::lock_guard<std::mutex> lk(allMu_);
                for (auto& j : all_) if (j->easy == easy) { job = j; break; }
            }
            if (!job) continue;
            FinishJob(job, msg->data.result);
            {
                std::lock_guard<std::mutex> lk(curlMu_);
                curl_multi_remove_handle(multi_, easy);
                curl_easy_cleanup(easy);
            }
            job->easy = nullptr;
            if (job->hdrList) { curl_slist_free_all(job->hdrList); job->hdrList = nullptr; }
            {
                std::lock_guard<std::mutex> lk(allMu_);
                for (auto it = all_.begin(); it != all_.end(); ++it) {
                    if (it->get() == job.get()) { all_.erase(it); break; }
                }
            }
        }

        ResumePausedJobs();

        if (!running_.load()) break;
        int numfds = 0;
        {
            std::lock_guard<std::mutex> lk(curlMu_);
            curl_multi_poll(multi_, nullptr, 0, stillRunning == 0 ? 5 : 1, &numfds);
        }
    }

    // 退出：把还在途的 handle 全部作废，避免 Manager 里的等待方卡死
    std::vector<JobPtr> jobs;
    {
        std::lock_guard<std::mutex> lk(allMu_);
        jobs.swap(all_);
    }
    for (auto& j : jobs) {
        if (j->easy) {
            curl_multi_remove_handle(multi_, j->easy);
            curl_easy_cleanup(j->easy);
            j->easy = nullptr;
        }
        if (j->hdrList) { curl_slist_free_all(j->hdrList); j->hdrList = nullptr; }
        std::lock_guard<std::mutex> lk(j->mu);
        j->ok = false;
        if (j->error.empty()) j->error = "shutdown";
        j->done = true;
        j->cvDone.notify_all();
    }
}

void Engine::WriterLoop() {
    while (true) {
        bool didWork = false;
        std::vector<SinkPtr> list;
        {
            std::lock_guard<std::mutex> lk(writerMu_);
            if (writerStop_ && sinks_.empty()) return;
            list = sinks_;
        }

        for (auto& s : list) {
            if (s->busy.test_and_set()) continue;   // 另一个写盘线程正在处理它
            std::vector<uint8_t> chunk;
            bool shouldClose = false;
            {
                std::lock_guard<std::mutex> slk(s->mu);
                if (!s->chunks.empty()) {
                    chunk = std::move(s->chunks.front());
                    s->chunks.pop_front();
                    s->queuedBytes -= chunk.size();
                } else if (s->streamEnded) {
                    shouldClose = true;
                }
            }

            if (!chunk.empty()) {
                didWork = true;
                if (!s->file) {
                    s->file = _wfopen(s->path.c_str(), s->append ? L"ab" : L"wb");
                    if (!s->file) {
                        std::lock_guard<std::mutex> slk(s->mu);
                        s->failed = true;
                        s->chunks.clear();
                        s->queuedBytes = 0;
                        s->streamEnded = true;
                    }
                }
                if (s->file) {
                    if (s->baseOffset) _fseeki64(s->file, (long long)(s->baseOffset + s->writtenBytes), SEEK_SET);
                    std::fwrite(chunk.data(), 1, chunk.size(), s->file);
                    std::lock_guard<std::mutex> slk(s->mu);
                    s->writtenBytes += chunk.size();
                    if (s->streamEnded && s->chunks.empty()) {
                        std::fflush(s->file);
                        std::fclose(s->file);
                        s->file = nullptr;
                        s->closed.store(true);
                    }
                } else {
                    s->closed.store(true);   // 打开失败，没有句柄可关
                }
            }

            if (shouldClose || s->failed) {
                if (s->file) { std::fflush(s->file); std::fclose(s->file); s->file = nullptr; }
                s->closed.store(true);
                std::lock_guard<std::mutex> lk(writerMu_);
                for (auto it = sinks_.begin(); it != sinks_.end(); ++it) {
                    if (it->get() == s.get()) { sinks_.erase(it); break; }
                }
                didWork = true;
            }
            s->busy.clear();
        }

        if (!didWork) {
            std::unique_lock<std::mutex> lk(writerMu_);
            if (writerStop_) {
                // 收尾：剩下的数据尽量写完
                bool empty = true;
                for (auto& s : sinks_) {
                    std::lock_guard<std::mutex> slk(s->mu);
                    if (!s->chunks.empty()) { empty = false; break; }
                }
                if (empty) return;
                continue;
            }
            writerCv_.wait_for(lk, std::chrono::milliseconds(3));
        }
    }
}

} // namespace detail
} // namespace xgdl
