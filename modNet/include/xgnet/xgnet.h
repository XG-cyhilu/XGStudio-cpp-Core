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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// XGNET —— HTTP 客户端（Windows / C++17 / 静态库 / Apache-2.0）
//
// 同步请求，一个 Client 复用一个 easy handle：连接与 TLS 会话都会复用。
// 适合"拿个 JSON、查个版本号、调个 API"，不适合大文件下载（那是 XGDL 的活）。
// 调用方自己放到后台线程（配 XGTHREAD::Pool 即可），库内不另起线程。

namespace xgnet {

struct Request {
    std::string url;                                        // http/https，UTF-8
    std::string method = "GET";                             // GET/POST/PUT/PATCH/DELETE/HEAD
    std::vector<std::pair<std::string, std::string>> headers;   // "Accept: application/json"
    std::string body;                                       // POST/PUT 的负载
    uint32_t connectTimeoutMs = 10'000;
    uint32_t timeoutMs = 30'000;                            // 0 = 不限
    bool followRedirects = true;
    int maxRedirects = 5;
    bool verifyPeer = true;                                 // 关掉等于不校验证书，只给调试用
    std::string proxy;                                      // 空 = 直连
    std::string userAgent = "XGNet/1.0";
};

struct Response {
    long status = 0;                                        // HTTP 状态码；0 = 没连上
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string effectiveUrl;
    double totalSec = 0.0;
    uint64_t received = 0;                                  // body 字节数
    std::string error;                                      // 传输层错误；空 = 传输成功

    bool TransportOk() const { return status != 0; }
    bool IsSuccess() const { return status >= 200 && status < 300; }
    bool Header(const std::string& name, std::string& out) const;
};

class Client {
public:
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // 同步执行；返回 false 时看 Response::error（HTTP 4xx/5xx 仍返回 true）
    bool Perform(const Request& req, Response& res);

    // 常用快捷方式
    bool Get(const std::string& url, Response& res, uint32_t timeoutMs = 30'000);
    bool PostJson(const std::string& url, const std::string& json, Response& res,
                  uint32_t timeoutMs = 30'000);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 一次性便利函数：内部临时建一个 Client（不复用连接，适合单发）
bool Get(const std::string& url, Response& res, uint32_t timeoutMs = 30'000);
bool PostJson(const std::string& url, const std::string& json, Response& res,
              uint32_t timeoutMs = 30'000);

} // namespace xgnet
