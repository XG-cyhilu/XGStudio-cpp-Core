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

// Offline-only: encoding, header helpers and a loopback request against a socket
// this test opens itself. No dependency on the internet, so CI cannot flake.

#include <xgnet/xgnet.h>

#include "../test_util.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace {

// Minimal HTTP/1.1 server: answers every request with one fixed response, then stops.
class LoopServer {
public:
    bool Start(const std::string& rawResponse) {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

        sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_ == INVALID_SOCKET) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) return false;
        if (listen(sock_, 4) != 0) return false;

        sockaddr_in bound{};
        int len = sizeof bound;
        if (getsockname(sock_, reinterpret_cast<sockaddr*>(&bound), &len) != 0) return false;
        port_ = ntohs(bound.sin_port);

        worker_ = std::thread([this, rawResponse] {
            SOCKET c = accept(sock_, nullptr, nullptr);
            if (c != INVALID_SOCKET) {
                std::string req(4096, '\0');
                recv(c, req.data(), static_cast<int>(req.size()), 0);
                send(c, rawResponse.data(), static_cast<int>(rawResponse.size()), 0);
                shutdown(c, SD_BOTH);
                closesocket(c);
            }
        });
        return true;
    }

    std::string Url(const char* path) { return "http://127.0.0.1:" + std::to_string(port_) + path; }

    void Stop() {
        if (worker_.joinable()) worker_.join();
        if (sock_ != INVALID_SOCKET) closesocket(sock_);
        sock_ = INVALID_SOCKET;
        WSACleanup();
    }

private:
    SOCKET sock_ = INVALID_SOCKET;
    uint16_t port_ = 0;
    std::thread worker_;
};

} // namespace

int main() {
    // ---- percent encoding ----
    const std::string raw = "a b&c=d/中";
    const std::string enc = xgnet::UrlEncode(raw);
    XGT_CHECK(enc == "a%20b%26c%3Dd%2F%E4%B8%AD");
    XGT_CHECK(xgnet::UrlDecode(enc) == raw);
    XGT_CHECK(xgnet::UrlDecode("%zz") == "%zz");   // invalid escape kept as-is

    // ---- query ----
    auto pairs = xgnet::QueryDecode("q=hello%20world&lang=%E4%B8%AD&empty=&q=second");
    XGT_CHECK(pairs.size() == 4);
    XGT_CHECK(pairs[0].first == "q" && pairs[0].second == "hello world");
    XGT_CHECK(pairs[1].first == "lang" && pairs[1].second == "中");
    XGT_CHECK(pairs[2].first == "empty" && pairs[2].second.empty());
    XGT_CHECK(pairs[3].first == "q" && pairs[3].second == "second");

    std::string q = xgnet::QueryEncode({ { "a", "1 2" }, { "b", "中" } });
    XGT_CHECK(q == "a=1%202&b=%E4%B8%AD");
    XGT_CHECK(xgnet::QueryDecode(q).size() == 2);

    // ---- Request::HasHeader / Response::Header ----
    xgnet::Request req;
    req.headers.push_back({ "Accept", "application/json" });
    XGT_CHECK(req.HasHeader("accept"));
    XGT_CHECK(!req.HasHeader("content-type"));

    xgnet::Response res;
    res.headers.push_back({ "Content-Type", "text/plain" });
    std::string hv;
    XGT_CHECK(res.Header("content-type", hv) && hv == "text/plain");
    XGT_CHECK(!res.Header("x-missing", hv));

    // ---- loopback plain request ----
    {
        LoopServer srv;
        XGT_CHECK(srv.Start("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 8\r\n\r\nloopback"));
        xgnet::Client cli;
        xgnet::Response r;
        XGT_CHECK(cli.Get(srv.Url("/ping"), r, 5000));
        XGT_CHECK(r.status == 200);
        XGT_CHECK(r.body == "loopback");
        XGT_CHECK(r.received == 8);
        std::string ct;
        XGT_CHECK(r.Header("content-type", ct) && ct == "text/plain");
        srv.Stop();
    }

    // ---- proxy probe must not crash regardless of machine settings ----
    {
        std::string pac;
        std::string proxy = xgnet::SystemProxy(&pac);
        XGT_CHECK(proxy.empty() || proxy.find('=') != std::string::npos || proxy.find(':') != std::string::npos);
    }

    XGT_FINISH("net");
}
