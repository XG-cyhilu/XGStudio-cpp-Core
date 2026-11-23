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
//
// 两种模式都走与 UI 完全一致的轮询路径（Snapshot + PollEvent）。

#include <xgdl/xgdl.h>

#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

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

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 3 && wcscmp(argv[1], L"--batch") == 0) return RunBatch(argv[2]);
    if (argc < 3) {
        std::wprintf(L"用法: xgdl_demo <url> <保存路径> [sha1] [预计字节数]\n");
        std::wprintf(L"      xgdl_demo --batch <清单文件>\n");
        return 1;
    }
    return RunSingle(argc, argv);
}
