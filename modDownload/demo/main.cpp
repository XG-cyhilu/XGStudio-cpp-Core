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

// 用法：
//   xgdl_demo <url> <保存路径> [sha1] [预计字节数]      单任务，每 200ms 打进度
//   xgdl_demo --batch <清单文件>                       每行 "URL<TAB>保存路径"
//   xgdl_demo --selfcheck                              本地回环自检（镜像回退/断点续传/摘要/请求头）
//
// 两种联网模式都走与 UI 完全一致的轮询路径（Snapshot + PollEvent）。

#include <xgdl/xgdl.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace {

void PrintProgress(const xgdl::Progress& p) {
    double mbDone = (double)p.bytesDone / (1024.0 * 1024.0);
    double mbTotal = (double)p.bytesTotal / (1024.0 * 1024.0);
    double mbps = p.bytesPerSec / (1024.0 * 1024.0);
    if (p.bytesTotal)
        std::wprintf(L"\r%7.2f / %7.2f MB  %6.2f MB/s  活动 %u  排队 %u  完成 %u  失败 %u",
                     mbDone, mbTotal, mbps, p.active, p.queued, p.completed, p.failed);
    else
        std::wprintf(L"\r%7.2f MB  %6.2f MB/s  活动 %u  排队 %u  完成 %u  失败 %u",
                     mbDone, mbps, p.active, p.queued, p.completed, p.failed);
}

int RunSingle(int argc, wchar_t** argv) {
    std::wstring url = argv[1];
    std::wstring path = argv[2];
    std::wstring sha1 = argc > 3 ? argv[3] : L"";
    uint64_t expect = argc > 4 ? _wcstoui64(argv[4], nullptr, 10) : 0;

    xgdl::Config cfg;
    cfg.smallThreads = 8;
    cfg.largeThreads = 4;
    cfg.chunkSize = 4ull << 20;

    xgdl::Manager mgr(cfg);
    uint32_t id = mgr.Add(url, path, expect, sha1, xgdl::Priority::High);
    if (!id) {
        std::wprintf(L"入队失败（参数非法或路径重复）\n");
        return 1;
    }
    mgr.Start();

    while (true) {
        Sleep(200);
        PrintProgress(mgr.Snapshot());

        xgdl::Event ev;
        if (mgr.PollEvent(ev)) {
            std::wprintf(L"\n");
            if (ev.kind == xgdl::EventKind::Completed) {
                std::wprintf(L"完成: %s\n", ev.path.c_str());
                mgr.Shutdown();
                return 0;
            }
            std::wprintf(L"失败: HTTP %ld  %s\n", ev.httpStatus, ev.message.c_str());
            mgr.Shutdown();
            return 2;
        }

        xgdl::TaskInfo info;
        if (mgr.QueryTask(id, info) && info.state == xgdl::TaskState::Failed) {
            std::wprintf(L"\n任务失败\n");
            mgr.Shutdown();
            return 2;
        }
    }
}

int RunBatch(const wchar_t* listFile) {
    std::FILE* f = _wfopen(listFile, L"rt, ccs=UTF-8");
    if (!f) {
        std::wprintf(L"打不开清单文件\n");
        return 1;
    }
    std::vector<std::pair<std::wstring, std::wstring>> items;
    wchar_t line[4096];
    while (fgetws(line, 4096, f)) {
        std::wstring s(line);
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
        if (s.empty() || s[0] == L'#') continue;
        size_t tab = s.find(L'\t');
        if (tab == std::wstring::npos) continue;
        items.emplace_back(s.substr(0, tab), s.substr(tab + 1));
    }
    std::fclose(f);

    xgdl::Config cfg;
    cfg.smallThreads = 16;
    cfg.largeThreads = 4;

    xgdl::Manager mgr(cfg);
    int added = 0;
    for (auto& it : items)
        if (mgr.Add(it.first, it.second, 0, L"", xgdl::Priority::Normal)) ++added;
    mgr.Start();

    uint32_t okCount = 0, failCount = 0;
    while (okCount + failCount < (uint32_t)added) {
        Sleep(200);
        PrintProgress(mgr.Snapshot());
        xgdl::Event ev;
        while (mgr.PollEvent(ev)) {
            if (ev.kind == xgdl::EventKind::Completed) ++okCount;
            else { ++failCount; std::wprintf(L"\n失败: %s  %s\n", ev.path.c_str(), ev.message.c_str()); }
        }
    }
    std::wprintf(L"\n批量结束: 成功 %u / %d，失败 %u\n", okCount, added, failCount);
    mgr.Shutdown();
    return failCount ? 2 : 0;
}

// ---------------- 本地回环自检 ----------------

bool Sha256Hex(const std::wstring& path, std::wstring& hex) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> obj, digest(32);
    bool ok = false;
    std::FILE* f = nullptr;
    DWORD objLen = 0, cb = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0);
    obj.resize(objLen);
    if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) < 0 ||
        !(f = _wfopen(path.c_str(), L"rb"))) {
        if (f) std::fclose(f);
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }
    std::vector<uint8_t> buf(64 * 1024);
    size_t n = 0;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0)
        BCryptHashData(hash, buf.data(), (ULONG)n, 0);
    std::fclose(f);
    if (BCryptFinishHash(hash, digest.data(), (ULONG)digest.size(), 0) >= 0) {
        hex.clear();
        static const wchar_t* d = L"0123456789abcdef";
        for (uint8_t b : digest) { hex.push_back(d[b >> 4]); hex.push_back(d[b & 0xF]); }
        ok = true;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

// 只够自检用的极小 HTTP 服务：/bad 返回 404，其余按 Range 返回 206
struct TestServer {
    SOCKET listenSock = INVALID_SOCKET;
    std::thread worker;
    std::atomic<bool> stop{ false };
    std::vector<uint8_t> payload;
    std::atomic<int> rangeStart{ -1 };
    std::atomic<int> headerSeen{ 0 };
    std::atomic<int> requests{ 0 };
    uint16_t port = 0;

    bool Start() {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
        listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock == INVALID_SOCKET) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) return false;
        if (listen(listenSock, 8) == SOCKET_ERROR) return false;
        sockaddr_in bound{};
        int len = sizeof(bound);
        getsockname(listenSock, (sockaddr*)&bound, &len);
        port = ntohs(bound.sin_port);
        worker = std::thread([this] { Loop(); });
        return true;
    }

    void Stop() {
        stop.store(true);
        if (listenSock != INVALID_SOCKET) { closesocket(listenSock); listenSock = INVALID_SOCKET; }
        if (worker.joinable()) worker.join();
    }

    void Loop() {
        while (!stop.load()) {
            SOCKET c = accept(listenSock, nullptr, nullptr);
            if (c == INVALID_SOCKET) return;
            Serve(c);
            closesocket(c);
        }
    }

    void Serve(SOCKET c) {
        std::string req;
        char buf[2048];
        while (req.find("\r\n\r\n") == std::string::npos) {
            int n = recv(c, buf, sizeof(buf), 0);
            if (n <= 0) return;
            req.append(buf, (size_t)n);
        }
        ++requests;
        if (req.find("X-Test: 1") != std::string::npos) headerSeen.store(1);

        int start = 0, end = -1;
        size_t rp = req.find("Range: bytes=");
        if (rp != std::string::npos) {
            const char* p = req.c_str() + rp + 13;
            start = std::atoi(p);
            const char* dash = std::strchr(p, '-');
            if (dash && dash[1] >= '0' && dash[1] <= '9') end = std::atoi(dash + 1);
            rangeStart.store(start);
        }

        bool bad = req.find("GET /bad ") == 0;
        int last = end >= 0 ? std::min(end, (int)payload.size() - 1) : (int)payload.size() - 1;
        bool partial = rp != std::string::npos && start < (int)payload.size();

        std::string head;
        std::vector<uint8_t> body;
        if (bad) {
            head = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        } else {
            head = std::string("HTTP/1.1 ") + (partial ? "206 Partial Content" : "200 OK") + "\r\n";
            head += "Accept-Ranges: bytes\r\n";
            if (partial) {
                head += "Content-Range: bytes " + std::to_string(start) + "-" +
                        std::to_string(last) + "/" + std::to_string(payload.size()) + "\r\n";
                body.assign(payload.begin() + start, payload.begin() + last + 1);
            } else {
                body = payload;
            }
            head += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
        }
        send(c, head.c_str(), (int)head.size(), 0);
        if (!body.empty()) send(c, (const char*)body.data(), (int)body.size(), 0);
    }
};

int WaitOne(xgdl::Manager& mgr, uint32_t id, xgdl::Event& ev) {
    while (true) {
        Sleep(20);
        if (mgr.PollEvent(ev)) {
            if (ev.id == id) return 0;
            continue;
        }
        xgdl::TaskInfo info;
        if (mgr.QueryTask(id, info) && info.state == xgdl::TaskState::Failed) {
            ev.kind = xgdl::EventKind::Failed;
            ev.id = id;
            return 0;
        }
    }
}

int RunSelfCheck() {
    const std::wstring dir = L".\\.build\\dlselfcheck";
    CreateDirectoryW(L".\\.build", nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);

    TestServer srv;
    srv.payload.resize(300 * 1024);
    for (size_t i = 0; i < srv.payload.size(); ++i) srv.payload[i] = (uint8_t)((i * 31 + 7) & 0xFF);
    if (!srv.Start()) { std::wprintf(L"本地服务启动失败\n"); return 1; }

    std::wstring base = L"http://127.0.0.1:" + std::to_wstring(srv.port);

    // 先把参考文件落在盘上，用它算期望的 SHA-256
    std::wstring refPath = dir + L"\\ref.bin";
    {
        std::FILE* f = _wfopen(refPath.c_str(), L"wb");
        std::fwrite(srv.payload.data(), 1, srv.payload.size(), f);
        std::fclose(f);
    }
    std::wstring sha256Ref;
    Sha256Hex(refPath, sha256Ref);

    xgdl::Config cfg;
    cfg.smallThreads = 4;
    cfg.largeThreads = 2;
    xgdl::Manager mgr(cfg);
    mgr.Start();

    int failures = 0;

    // [1] 主 URL 404 + 镜像可用 + SHA-256 校验 + 请求头
    {
        std::wstring out = dir + L"\\mirror.bin";
        _wremove(out.c_str());
        xgdl::TaskOptions opt;
        opt.expectBytes = srv.payload.size();
        opt.hash.alg = xgdl::HashAlg::Sha256;
        opt.hash.hex = sha256Ref;
        opt.mirrors.push_back(base + L"/file");
        opt.headers.emplace_back(L"X-Test", L"1");
        uint32_t id = mgr.Add(base + L"/bad", out, opt);
        xgdl::Event ev;
        WaitOne(mgr, id, ev);
        xgdl::TaskInfo info;
        mgr.QueryTask(id, info);
        uint64_t sz = 0;
        bool sizeOk = GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES;
        std::FILE* f = _wfopen(out.c_str(), L"rb");
        if (f) { _fseeki64(f, 0, SEEK_END); sz = (uint64_t)_ftelli64(f); std::fclose(f); }
        bool ok = ev.kind == xgdl::EventKind::Completed && sz == srv.payload.size() &&
                  info.attempt == 1 && srv.headerSeen.load() == 1 && sizeOk;
        std::wprintf(L"[1] mirror-failover state=%d attempt=%d size=%llu header=%d => %s\n",
                     (int)info.state, info.attempt, (unsigned long long)sz, srv.headerSeen.load(),
                     ok ? L"OK" : L"FAIL");
        if (!ok) ++failures;
    }

    // [2] 断点续传：残留 120KB 的 .part，服务器应看到 Range: bytes=120000
    {
        std::wstring out = dir + L"\\resume.bin";
        std::wstring part = out + L".part";
        _wremove(out.c_str());
        _wremove(part.c_str());
        std::FILE* f = _wfopen(part.c_str(), L"wb");
        std::fwrite(srv.payload.data(), 1, 120000, f);
        std::fclose(f);

        xgdl::TaskOptions opt;
        opt.expectBytes = srv.payload.size();
        opt.hash.alg = xgdl::HashAlg::Sha256;
        opt.hash.hex = sha256Ref;
        uint32_t id = mgr.Add(base + L"/file", out, opt);
        xgdl::Event ev;
        WaitOne(mgr, id, ev);

        uint64_t sz = 0;
        std::FILE* rf = _wfopen(out.c_str(), L"rb");
        bool same = false;
        if (rf) {
            _fseeki64(rf, 0, SEEK_END); sz = (uint64_t)_ftelli64(rf);
            std::fclose(rf);
            std::wstring h; Sha256Hex(out, h);
            same = (h == sha256Ref);
        }
        bool ok = ev.kind == xgdl::EventKind::Completed && sz == srv.payload.size() && same &&
                  srv.rangeStart.load() == 120000;
        std::wprintf(L"[2] resume rangeStart=%d size=%llu content=%d => %s\n",
                     srv.rangeStart.load(), (unsigned long long)sz, (int)same, ok ? L"OK" : L"FAIL");
        if (!ok) ++failures;
    }

    // [3] 摘要不符：必须失败并删掉文件
    {
        std::wstring out = dir + L"\\badhash.bin";
        _wremove(out.c_str());
        xgdl::TaskOptions opt;
        opt.expectBytes = srv.payload.size();
        opt.hash.alg = xgdl::HashAlg::Sha256;
        opt.hash.hex = std::wstring(64, L'0');
        uint32_t id = mgr.Add(base + L"/file", out, opt);
        xgdl::Event ev;
        WaitOne(mgr, id, ev);
        xgdl::TaskInfo info;
        mgr.QueryTask(id, info);
        bool gone = GetFileAttributesW(out.c_str()) == INVALID_FILE_ATTRIBUTES;
        bool ok = info.state == xgdl::TaskState::Failed && gone;
        std::wprintf(L"[3] hash-mismatch state=%d removed=%d msgLen=%zu => %s\n",
                     (int)info.state, (int)gone, ev.message.size(), ok ? L"OK" : L"FAIL");
        if (!ok) ++failures;
    }

    // [4] 大文件走分块：合并到 .part 再改名，内容要完整
    {
        TestServer big;
        big.payload.resize(3u << 20);
        for (size_t i = 0; i < big.payload.size(); ++i) big.payload[i] = (uint8_t)((i * 17 + 3) & 0xFF);
        if (!big.Start()) { std::wprintf(L"[4] 大文件服务启动失败\n"); ++failures; }
        else {
            std::wstring out = dir + L"\\chunked.bin";
            std::wstring refBig = dir + L"\\chunked-ref.bin";
            _wremove(out.c_str());
            {
                std::FILE* f = _wfopen(refBig.c_str(), L"wb");
                std::fwrite(big.payload.data(), 1, big.payload.size(), f);
                std::fclose(f);
            }
            std::wstring shaBig;
            Sha256Hex(refBig, shaBig);

            xgdl::Config bcfg;
            bcfg.largeThreshold = 512 * 1024;
            bcfg.chunkSize = 512 * 1024;
            bcfg.largeThreads = 3;
            xgdl::Manager bigMgr(bcfg);
            bigMgr.Start();

            xgdl::TaskOptions opt;
            opt.expectBytes = big.payload.size();
            opt.hash.alg = xgdl::HashAlg::Sha256;
            opt.hash.hex = shaBig;
            uint32_t id = bigMgr.Add(L"http://127.0.0.1:" + std::to_wstring(big.port) + L"/file", out, opt);
            xgdl::Event ev;
            WaitOne(bigMgr, id, ev);
            bigMgr.Shutdown();
            big.Stop();

            uint64_t sz = 0;
            std::FILE* rf = _wfopen(out.c_str(), L"rb");
            std::wstring h;
            if (rf) { _fseeki64(rf, 0, SEEK_END); sz = (uint64_t)_ftelli64(rf); std::fclose(rf); Sha256Hex(out, h); }
            bool ok = ev.kind == xgdl::EventKind::Completed && sz == big.payload.size() && h == shaBig;
            std::wprintf(L"[4] chunked size=%llu content=%d eventKind=%d http=%ld msg=%s => %s\n",
                         (unsigned long long)sz, (int)(h == shaBig), (int)ev.kind, ev.httpStatus,
                         ev.message.c_str(), ok ? L"OK" : L"FAIL");
            if (!ok) ++failures;
        }
    }

    mgr.Shutdown();
    srv.Stop();
    std::wprintf(L"selfcheck done: %s (%d 项失败)，请求数 %d\n", failures ? L"FAILED" : L"PASSED",
                 failures, srv.requests.load());
    return failures ? 2 : 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 && wcscmp(argv[1], L"--selfcheck") == 0) return RunSelfCheck();
    if (argc >= 3 && wcscmp(argv[1], L"--batch") == 0) return RunBatch(argv[2]);
    if (argc < 3) {
        std::wprintf(L"用法: xgdl_demo <url> <保存路径> [sha1] [预计字节数]\n");
        std::wprintf(L"      xgdl_demo --batch <清单文件>\n");
        std::wprintf(L"      xgdl_demo --selfcheck\n");
        return 1;
    }
    return RunSingle(argc, argv);
}
