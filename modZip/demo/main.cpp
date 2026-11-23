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

#include <xgzip/xgzip.h>

#include <cstdio>
#include <string>

static void PrintList(const std::wstring& zip) {
    std::vector<xgzip::Entry> es;
    std::string err;
    if (!xgzip::List(zip, es, err)) {
        printf("list failed: %s\n", err.c_str());
        return;
    }
    uint64_t raw = 0, packed = 0;
    for (const auto& e : es) {
        raw += e.size;
        packed += e.compressedSize;
        printf("%-46ls %10llu -> %10llu  crc=%08x m=%u%s\n", e.name.c_str(),
               static_cast<unsigned long long>(e.size), static_cast<unsigned long long>(e.compressedSize), e.crc32,
               e.method, e.isDir ? " [dir]" : "");
    }
    printf("entries=%zu raw=%llu packed=%llu\n", es.size(), static_cast<unsigned long long>(raw),
           static_cast<unsigned long long>(packed));
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        printf("usage:\n  xgzip_demo list <zip>\n  xgzip_demo extract <zip> <dest> [filter]\n");
        return 1;
    }
    std::wstring cmd = argv[1];
    std::wstring zip = argv[2];

    if (cmd == L"list") {
        PrintList(zip);
        return 0;
    }
    if (cmd == L"extract") {
        if (argc < 4) return 1;
        xgzip::ExtractOptions opt;
        if (argc >= 5) opt.filter = argv[4];
        xgzip::Result r;
        xgzip::Stats st;
        bool ok = xgzip::Extract(zip, argv[3], opt, r, &st);
        printf("ok=%d files=%d dirs=%d skipped=%d bytes=%llu elapsed=%lldms stats=%u/%u\n", ok, r.files, r.dirs,
               r.skipped, static_cast<unsigned long long>(r.bytes), static_cast<long long>(r.elapsedMs),
               st.filesDone.load(), st.filesTotal.load());
        if (!ok) {
            printf("error: %s (failed=%zu)\n", r.error.c_str(), r.failed.size());
            for (const auto& f : r.failed) printf("  ! %ls\n", f.c_str());
        }

        // 回读一个条目，验证内存路径
        std::vector<xgzip::Entry> es;
        std::string err;
        if (xgzip::List(zip, es, err)) {
            for (const auto& e : es) {
                if (e.isDir || e.size > 1024 * 1024) continue;
                std::vector<uint8_t> mem;
                if (xgzip::ReadEntry(zip, e.name, mem, err))
                    printf("readback %ls bytes=%zu ok\n", e.name.c_str(), mem.size());
                break;
            }
        }
        return ok ? 0 : 1;
    }
    printf("unknown command\n");
    return 1;
}
