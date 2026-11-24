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

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

// 写一个混合内容的包：文本（高压缩比）、随机二进制（不可压）、中文名、目录条目，
// 然后用自家的 List/ReadEntry 回读校验字节数与 CRC。
static int WriteTest(const std::wstring& zip, xgzip::WriteMethod method) {
    std::vector<uint8_t> text, bin;
    std::string para = "xgzip writer: store and deflate both go through the same entry writer.\n";
    for (int i = 0; i < 800; ++i) text.insert(text.end(), para.begin(), para.end());
    bin.resize(64 * 1024);
    for (size_t i = 0; i < bin.size(); ++i) bin[i] = (uint8_t)((i * 37 + (i >> 3)) & 0xFF);

    xgzip::WriteOptions opt;
    opt.method = method;
    xgzip::Writer w(zip, opt);
    std::string err;
    if (!w.Begin(err)) { printf("begin failed: %s\n", err.c_str()); return 1; }
    if (!w.AddDirectory(L"docs", err)) { printf("dir failed: %s\n", err.c_str()); return 1; }
    if (!w.AddMemory(L"docs\\长文本.txt", text.data(), text.size(), err)) {
        printf("addMemory failed: %s\n", err.c_str());
        return 1;
    }
    if (!w.AddMemory(L"bin\\random.bin", bin.data(), bin.size(), err)) {
        printf("addMemory failed: %s\n", err.c_str());
        return 1;
    }

    // 磁盘文件：真随机 300KB，Store 走流式复制，Deflate 会因压不小而回退成 store
    std::vector<uint8_t> rnd(300 * 1024);
    uint32_t seed = 0x12345678;
    for (size_t i = 0; i < rnd.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        rnd[i] = (uint8_t)(seed >> 24);
    }
    std::wstring diskSrc = zip + L".src.bin";
    {
        std::FILE* f = nullptr;
        _wfopen_s(&f, diskSrc.c_str(), L"wb");
        if (!f) { printf("cannot create temp source\n"); return 1; }
        std::fwrite(rnd.data(), 1, rnd.size(), f);
        std::fclose(f);
    }
    if (!w.AddFile(L"bin\\random-from-disk.bin", diskSrc, err)) {
        printf("addFile failed: %s\n", err.c_str());
        DeleteFileW(diskSrc.c_str());
        return 1;
    }
    if (!w.Commit(err)) { printf("commit failed: %s\n", err.c_str()); return 1; }

    printf("written %ls: entries=%u bytes=%llu method=%s\n", zip.c_str(), w.EntriesWritten(),
           (unsigned long long)w.BytesWritten(), method == xgzip::WriteMethod::Deflate ? "deflate" : "store");

    // 回读
    std::vector<xgzip::Entry> es;
    if (!xgzip::List(zip, es, err)) { printf("relist failed: %s\n", err.c_str()); return 1; }
    bool ok = true;
    for (auto& e : es) {
        printf("  %-40ls size=%llu csize=%llu method=%u crc=%08x%s\n", e.name.c_str(),
               (unsigned long long)e.size, (unsigned long long)e.compressedSize, e.method, e.crc32,
               e.isDir ? " [dir]" : "");
        if (e.isDir) continue;
        std::vector<uint8_t> data;
        if (!xgzip::ReadEntry(zip, e.name, data, err)) { printf("readback failed: %s\n", err.c_str()); return 1; }
        const std::vector<uint8_t>* src = nullptr;
        if (e.name.find(L"random-from-disk") != std::wstring::npos) src = &rnd;
        else if (e.name.find(L"txt") != std::wstring::npos) src = &text;
        else src = &bin;
        if (data.size() != src->size() || std::memcmp(data.data(), src->data(), data.size()) != 0) {
            printf("  content mismatch on %ls\n", e.name.c_str());
            ok = false;
        }
    }
    DeleteFileW(diskSrc.c_str());
    printf("write %s: %s\n", method == xgzip::WriteMethod::Deflate ? "deflate" : "store",
           ok ? "OK" : "FAIL");
    return ok ? 0 : 2;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        printf("usage:\n  xgzip_demo list <zip>\n  xgzip_demo extract <zip> <dest> [filter]\n"
               "  xgzip_demo write <zip> [store|deflate]\n");
        return 1;
    }
    std::wstring cmd = argv[1];
    std::wstring zip = argv[2];

    if (cmd == L"write") {
        xgzip::WriteMethod m = xgzip::WriteMethod::Store;
        if (argc >= 4 && wcscmp(argv[3], L"deflate") == 0) m = xgzip::WriteMethod::Deflate;
        return WriteTest(zip, m);
    }

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
