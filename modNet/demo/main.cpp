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

#include <windows.h>

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
    std::string ce;
    printf("[1] content-encoding=%s (curl 已自动解压)\n", r1.Header("Content-Encoding", ce) ? ce.c_str() : "(identity)");

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

    // 7) URL / query 编解码（离线，可当回归用）
    {
        std::string raw = "a b&c=d/中文";
        std::string enc = xgnet::UrlEncode(raw);
        bool rt = xgnet::UrlDecode(enc) == raw;
        printf("[7] urlencode=%s roundtrip=%d\n", enc.c_str(), (int)rt);

        std::vector<std::pair<std::string, std::string>> pairs = {
            {"q", "hello world"}, {"lang", "中文"}, {"empty", ""}, {"q", "second"}};
        std::string query = xgnet::QueryEncode(pairs);
        auto back = xgnet::QueryDecode(query + "&plus=a+b");
        bool qok = query.find("q=hello%20world") != std::string::npos && back.size() == 5 &&
                   back[1].second == "中文" && back[3].second == "second" && back[4].second == "a b";
        printf("[7] query=%s parsed=%zu q-ok=%d => %s\n", query.c_str(), back.size(), (int)qok,
               (rt && qok) ? "OK" : "FAIL");
    }

    // 8) 系统代理探测（只报告，不发起请求）
    {
        std::string pac;
        std::string proxy = xgnet::SystemProxy(&pac);
        printf("[8] system-proxy=%s pac=%s\n", proxy.empty() ? "(direct)" : proxy.c_str(),
               pac.empty() ? "(none)" : pac.c_str());
    }

    // 9) 响应 gzip：npmmirror 的 packument 带 Accept-Encoding 时回 gzip。
    //    curl 解压后会从 header 流里隐去 Content-Encoding，所以判据是"body 是明文 JSON"
    {
        xgnet::Response r9;
        bool ok9 = client.Get("https://registry.npmmirror.com/react", r9, 20'000);
        std::string ce9;
        r9.Header("Content-Encoding", ce9);
        bool dec = ok9 && !r9.body.empty() && r9.body[0] == '{';
        printf("[9] gzip ok=%d status=%ld plaintext-json=%d decompressed=%d body=%llu ce-hdr=%s\n",
               (int)ok9, r9.status, (int)dec, (int)r9.decompressed,
               (unsigned long long)r9.body.size(), ce9.empty() ? "(none)" : ce9.c_str());
    }

    // 10) cookie jar：服务端 Set-Cookie → 跟随跳转后回显 cookie
    {
        std::wstring jarPath = L".build\\xgnet-cookies.txt";
        DeleteFileW(jarPath.c_str());
        client.SetCookieFile(".build\\xgnet-cookies.txt");
        xgnet::Response r10;
        bool ok10 = client.Get("https://httpbin.org/cookies/set?probe=xgnet", r10, 20'000);
        auto cookies = client.DumpCookies();
        bool echoed = r10.body.find("probe") != std::string::npos &&
                      r10.body.find("xgnet") != std::string::npos;
        printf("[10] cookie ok=%d status=%ld jar=%zu echoed=%d\n", (int)ok10, r10.status,
               cookies.size(), (int)echoed);
        client.ClearCookies();
        DeleteFileW(jarPath.c_str());
    }
    return 0;
}
