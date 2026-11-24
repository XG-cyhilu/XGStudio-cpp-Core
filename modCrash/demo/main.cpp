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

// xgcrash_demo            装好处理器，手写 3 个 dump，验证清理只留 2 个
// xgcrash_demo --crash    故意空指针解引用，验证 SEH 现场能抓到（会真的崩）

#include <xgcrash/xgcrash.h>

#include <windows.h>
#include <cstdio>
#include <cstring>

namespace {

uint64_t FileSize(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA ad{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &ad)) return 0;
    return ((uint64_t)ad.nFileSizeHigh << 32) | ad.nFileSizeLow;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    bool crash = argc > 1 && wcscmp(argv[1], L"--crash") == 0;

    xgcrash::Options opt;
    opt.maxDumps = 2;
    opt.catchVectored = true;
    if (!crash) {
        // 演示用独立目录，别污染真实应用目录
        opt.dumpDir = xgcrash::DefaultDumpDir() + L"\\demo";
    }
    xgcrash::Install(opt);

    if (crash) {
        std::wprintf(L"[crash] handler installed, dir=%s\n", opt.dumpDir.c_str());
        std::fflush(stdout);
        volatile int* p = nullptr;
        *p = 1;                      // 访问冲突，交给 SEH / VEH
        return 0;
    }

    for (int i = 0; i < 3; ++i) {
        std::wstring p;
        bool ok = xgcrash::WriteDump(nullptr, L"manual", &p);
        std::wprintf(L"[%d] dump ok=%d path=%s size=%llu\n", i + 1, (int)ok, p.c_str(),
                     (unsigned long long)FileSize(p));
    }

    auto dumps = xgcrash::ListDumps();
    std::wprintf(L"[list] count=%zu (maxDumps=2)\n", dumps.size());
    for (auto& d : dumps) std::wprintf(L"  %s  %llu\n", d.c_str(), (unsigned long long)FileSize(d));

    xgcrash::Shutdown();
    std::wprintf(L"done: %s\n", dumps.size() <= 2 ? L"OK" : L"FAIL");
    return dumps.size() <= 2 ? 0 : 2;
}
