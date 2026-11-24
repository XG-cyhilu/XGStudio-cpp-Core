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

#include "gzip.h"

#include <curl/curl.h>
#include <windows.h>

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

bool RegDword(HKEY root, const wchar_t* sub, const wchar_t* name, DWORD& out) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, sz = sizeof(out);
    bool ok = RegQueryValueExW(k, name, nullptr, &type, (BYTE*)&out, &sz) == ERROR_SUCCESS &&
              type == REG_DWORD;
    RegCloseKey(k);
    return ok;
}

bool RegString(HKEY root, const wchar_t* sub, const wchar_t* name, std::string& out) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[1024] = {};
    DWORD type = 0, sz = sizeof(buf);
    bool ok = RegQueryValueExW(k, name, nullptr, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS &&
              type == REG_SZ;
    RegCloseKey(k);
    if (!ok) return false;
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    out.resize((size_t)(n > 0 ? n - 1 : 0));
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, buf, -1, out.data(), n, nullptr, nullptr);
    return true;
}

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

bool Request::HasHeader(const std::string& name) const {
    for (const auto& h : headers)
        if (_stricmp(h.first.c_str(), name.c_str()) == 0) return true;
    return false;
}

struct Client::Impl {
    CURL* easy = nullptr;
    std::mutex mu;
    std::string cookieFile;

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
    // 这份静态 libcurl 没链 zlib，不能让它自己解压：自己声明 gzip，收到后本库 gunzip
    if (req.acceptGzip && !req.HasHeader("Accept-Encoding"))
        headers = curl_slist_append(headers, "Accept-Encoding: gzip");

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

    if (!req.proxy.empty()) {
        curl_easy_setopt(h, CURLOPT_PROXY, req.proxy.c_str());
    } else if (req.useSystemProxy) {
        std::string sys = SystemProxy();
        if (!sys.empty()) curl_easy_setopt(h, CURLOPT_PROXY, sys.c_str());
    }
    if (headers) curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);

    std::string jar = req.cookieFile.empty() ? impl_->cookieFile : req.cookieFile;
    if (!jar.empty()) {
        curl_easy_setopt(h, CURLOPT_COOKIEFILE, jar.c_str());   // 载入
        curl_easy_setopt(h, CURLOPT_COOKIEJAR, jar.c_str());    // 结束时落盘
    }

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

    // gzip 解压：这份 libcurl 没链 zlib，收到 Content-Encoding: gzip 时由本库自己解
    std::string encoding;
    if (res.Header("Content-Encoding", encoding)) {
        std::string lower = encoding;
        for (auto& c : lower) c = (char)::tolower((unsigned char)c);
        if (lower.find("gzip") != std::string::npos) {
            std::string plain, gerr;
            if (!detail::Gunzip(res.body, plain, gerr)) {
                res.error = "gzip decode failed: " + gerr;
                res.received = res.body.size();
                return false;
            }
            res.body.swap(plain);
            res.decompressed = true;
        }
    }
    res.received = res.body.size();
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

// ---------------- cookie ----------------

void Client::SetCookieFile(const std::string& path) {
    impl_->cookieFile = path;
    if (!impl_->easy) return;
    std::lock_guard<std::mutex> lk(impl_->mu);
    curl_easy_setopt(impl_->easy, CURLOPT_COOKIEFILE, "");   // 打开内存 cookie 引擎
    if (!path.empty()) curl_easy_setopt(impl_->easy, CURLOPT_COOKIEFILE, path.c_str());
}

std::vector<std::string> Client::DumpCookies() {
    std::vector<std::string> out;
    if (!impl_->easy) return out;
    std::lock_guard<std::mutex> lk(impl_->mu);
    curl_easy_setopt(impl_->easy, CURLOPT_COOKIEFILE, "");
    curl_slist* list = nullptr;
    if (curl_easy_getinfo(impl_->easy, CURLINFO_COOKIELIST, &list) == CURLE_OK && list) {
        for (curl_slist* p = list; p; p = p->next) if (p->data) out.emplace_back(p->data);
        curl_slist_free_all(list);
    }
    return out;
}

void Client::ClearCookies() {
    if (!impl_->easy) return;
    std::lock_guard<std::mutex> lk(impl_->mu);
    curl_easy_setopt(impl_->easy, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(impl_->easy, CURLOPT_COOKIELIST, "ALL");
}

// ---------------- 工具 ----------------

std::string SystemProxy(std::string* autoConfigUrl) {
    const wchar_t* kSettings = L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";
    DWORD enable = 0;
    bool on = RegDword(HKEY_CURRENT_USER, kSettings, L"ProxyEnable", enable) && enable != 0;
    std::string pac;
    RegString(HKEY_CURRENT_USER, kSettings, L"AutoConfigURL", pac);
    if (autoConfigUrl) *autoConfigUrl = pac;
    if (!on) return {};

    std::string server;
    if (!RegString(HKEY_CURRENT_USER, kSettings, L"ProxyServer", server) || server.empty())
        return {};
    if (server.find('=') == std::string::npos) return server;   // "host:port"

    // 按协议列出："http=h:1;https=h:2;ftp=..."，优先 https，其次 http
    std::string best, fallback;
    size_t start = 0;
    while (start <= server.size()) {
        size_t sep = server.find(';', start);
        if (sep == std::string::npos) sep = server.size();
        std::string item = server.substr(start, sep - start);
        size_t eq = item.find('=');
        if (eq != std::string::npos) {
            std::string scheme = item.substr(0, eq);
            std::string addr = item.substr(eq + 1);
            if (scheme == "https") best = addr;
            else if (scheme == "http" && fallback.empty()) fallback = addr;
        }
        start = sep + 1;
    }
    return best.empty() ? fallback : best;
}

std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

std::string UrlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hv = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hv(s[i + 1]), lo = hv(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::string QueryEncode(const std::vector<std::pair<std::string, std::string>>& pairs) {
    std::string out;
    for (const auto& kv : pairs) {
        if (!out.empty()) out.push_back('&');
        out += UrlEncode(kv.first);
        out.push_back('=');
        out += UrlEncode(kv.second);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> QueryDecode(const std::string& query) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t start = 0;
    while (start <= query.size()) {
        size_t sep = query.find('&', start);
        if (sep == std::string::npos) sep = query.size();
        std::string item = query.substr(start, sep - start);
        if (!item.empty()) {
            size_t eq = item.find('=');
            std::string k = eq == std::string::npos ? item : item.substr(0, eq);
            std::string v = eq == std::string::npos ? std::string() : item.substr(eq + 1);
            std::replace(k.begin(), k.end(), '+', ' ');
            std::replace(v.begin(), v.end(), '+', ' ');
            out.emplace_back(UrlDecode(k), UrlDecode(v));
        }
        start = sep + 1;
    }
    return out;
}

} // namespace xgnet
