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

#include "xgnet/xgnet.h"

#include <curl/curl.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>

namespace xgnet {
namespace {

std::once_flag g_curlInit;

size_t WriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t n = size * nmemb;
    static_cast<Response*>(userdata)->body.append(ptr, n);
    return n;
}

size_t HeaderCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t n = size * nmemb;
    auto* res = static_cast<Response*>(userdata);
    std::string line(ptr, n);
    if (line.size() >= 5 && line.compare(0, 5, "HTTP/") == 0) {
        // 新响应开始（重定向后会有多个），清掉上一轮的 header
        res->headers.clear();
        return n;
    }
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
        std::string name = line.substr(0, colon);
        std::string v = line.substr(colon + 1);
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
        size_t s = v.find_first_not_of(' ');
        if (s != std::string::npos) v = v.substr(s);
        else v.clear();
        if (!name.empty()) res->headers.emplace_back(name, v);
    }
    return n;
}

std::string Trie(const char* s) { return s ? s : ""; }

} // namespace

bool Response::Header(const std::string& name, std::string& out) const {
    for (const auto& h : headers) {
        if (_stricmp(h.first.c_str(), name.c_str()) == 0) {
            out = h.second;
            return true;
        }
    }
    return false;
}

struct Client::Impl {
    CURL* easy = nullptr;
    std::mutex mu;

    Impl() {
        std::call_once(g_curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
        easy = curl_easy_init();
    }
    ~Impl() {
        if (easy) curl_easy_cleanup(easy);
    }
};

Client::Client() : impl_(new Impl) {}

Client::~Client() = default;

bool Client::Perform(const Request& req, Response& res) {
    res = Response{};
    if (!req.url.empty() && req.url.compare(0, 4, "http") != 0) {
        res.error = "only http/https urls are supported";
        return false;
    }
    if (!impl_->easy) {
        res.error = "curl init failed";
        return false;
    }

    std::lock_guard<std::mutex> lk(impl_->mu);
    CURL* h = impl_->easy;
    curl_easy_reset(h);

    curl_easy_setopt(h, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &res);
    curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, HeaderCb);
    curl_easy_setopt(h, CURLOPT_HEADERDATA, &res);
    curl_easy_setopt(h, CURLOPT_USERAGENT, req.userAgent.c_str());
    if (req.connectTimeoutMs) curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(req.connectTimeoutMs));
    if (req.timeoutMs) curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, static_cast<long>(req.timeoutMs));
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, req.followRedirects ? 1L : 0L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, static_cast<long>(req.maxRedirects));
    if (req.verifyPeer) curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
    else curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
#ifdef CURLSSLOPT_REVOKE_BEST_EFFORT
    // Schannel 的严格吊销检查在部分网络下会把正常证书判死：只降级吊销，不关校验
    curl_easy_setopt(h, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
#endif

    struct curl_slist* headers = nullptr;
    for (const auto& kv : req.headers) {
        std::string line = kv.first + ": " + kv.second;
        headers = curl_slist_append(headers, line.c_str());
    }

    std::string method = req.method;
    for (auto& c : method) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));

    bool hasBody = !req.body.empty();
    if (method == "POST") {
        curl_easy_setopt(h, CURLOPT_POST, 1L);
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
    } else if (method == "PUT" || method == "PATCH" || method == "DELETE") {
        curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (hasBody) {
            curl_easy_setopt(h, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
        }
    } else if (method == "HEAD") {
        curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, "HEAD");
    } else if (method != "GET") {
        curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method.c_str());
    }

    if (!req.proxy.empty()) curl_easy_setopt(h, CURLOPT_PROXY, req.proxy.c_str());
    if (headers) curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);

    CURLcode rc = curl_easy_perform(h);

    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    res.status = status;

    char* eff = nullptr;
    curl_easy_getinfo(h, CURLINFO_EFFECTIVE_URL, &eff);
    if (eff) res.effectiveUrl = eff;

    double total = 0;
    curl_easy_getinfo(h, CURLINFO_TOTAL_TIME, &total);
    res.totalSec = total;
    res.received = res.body.size();

    if (headers) curl_slist_free_all(headers);

    if (rc != CURLE_OK) {
        res.error = curl_easy_strerror(rc);
        res.status = 0;
        return false;
    }
    return true;
}

bool Client::Get(const std::string& url, Response& res, uint32_t timeoutMs) {
    Request req;
    req.url = url;
    req.timeoutMs = timeoutMs;
    return Perform(req, res);
}

bool Client::PostJson(const std::string& url, const std::string& json, Response& res, uint32_t timeoutMs) {
    Request req;
    req.url = url;
    req.method = "POST";
    req.headers.emplace_back("Content-Type", "application/json");
    req.headers.emplace_back("Accept", "application/json");
    req.body = json;
    req.timeoutMs = timeoutMs;
    return Perform(req, res);
}

bool Get(const std::string& url, Response& res, uint32_t timeoutMs) {
    Client c;
    return c.Get(url, res, timeoutMs);
}

bool PostJson(const std::string& url, const std::string& json, Response& res, uint32_t timeoutMs) {
    Client c;
    return c.PostJson(url, json, res, timeoutMs);
}

} // namespace xgnet
