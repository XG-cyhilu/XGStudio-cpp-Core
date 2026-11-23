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

#include <xgnet/xgnet.h>

#include <cstdio>
#include <string>

int main() {
    xgnet::Client client;

    // 1) HTTPS GET（按连接复用同一 easy handle）
    xgnet::Response r1;
    bool ok1 = client.Get("https://registry.npmmirror.com/typescript/latest", r1, 15'000);
    printf("[1] ok=%d status=%ld bytes=%llu total=%.3fs\n", (int)ok1, r1.status,
           static_cast<unsigned long long>(r1.received), r1.totalSec);
    printf("[1] body=%.100s\n", r1.body.c_str());
    std::string ct;
    printf("[1] content-type=%s\n", r1.Header("Content-Type", ct) ? ct.c_str() : "(none)");

    // 2) 第二次请求（同一 Client）应更快：连接与 TLS 会话复用
    xgnet::Response r2;
    bool ok2 = client.Get("https://registry.npmmirror.com/typescript/latest", r2, 15'000);
    printf("[2] ok=%d status=%ld total=%.3fs reused-faster=%d same-body=%d\n", (int)ok2, r2.status, r2.totalSec,
           (int)(r2.totalSec < r1.totalSec), (int)(r2.body == r1.body));

    // 3) 404
    xgnet::Response r3;
    bool ok3 = client.Get("https://registry.npmmirror.com/nope-404.tgz", r3, 15'000);
    printf("[3] transport ok=%d status=%ld\n", (int)ok3, r3.status);

    // 4) POST JSON
    xgnet::Response r4;
    // 回显服务：验证 POST body 与 Content-Type 确实被发出并被服务端读到
    bool ok4 = client.PostJson("https://postman-echo.com/post", "{\"probe\":true}", r4, 15'000);
    printf("[4] post ok=%d status=%ld bytes=%llu echoed=%d\n", (int)ok4, r4.status,
           static_cast<unsigned long long>(r4.received),
           (int)(r4.body.find("\"probe\":true") != std::string::npos));

    // 5) 连接超时（不可达地址）
    xgnet::Client c2;
    xgnet::Request req5;
    req5.url = "http://10.255.255.1/";
    req5.connectTimeoutMs = 600;
    req5.timeoutMs = 1200;
    xgnet::Response r5;
    bool ok5 = c2.Perform(req5, r5);
    printf("[5] timeout case ok=%d status=%ld err=%s\n", (int)ok5, r5.status, r5.error.c_str());

    // 6) 自定义请求头 + 错误 URL
    xgnet::Request req6;
    req6.url = "https://registry.npmmirror.com/typescript";
    req6.headers.emplace_back("Accept", "application/json");
    xgnet::Response r6;
    bool ok6 = client.Perform(req6, r6);
    printf("[6] header req ok=%d status=%ld accept-sent=1\n", (int)ok6, r6.status);

    xgnet::Response bad;
    bool ok6b = xgnet::Get("ftp://example.com", bad);
    printf("[6] bad scheme ok=%d err=%s\n", (int)ok6b, bad.error.c_str());
    return 0;
}
