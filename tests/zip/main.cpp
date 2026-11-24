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

#include "../test_util.h"

#include <numeric>

namespace {

// 64 KiB of highly compressible data (period 64), so deflate must shrink it.
std::string Repetitive() {
    std::string s(65536, '\0');
    for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<char>('A' + (i % 64));
    return s;
}

// Build one archive with a dir entry, an in-memory text and an on-disk blob;
// then read everything back and compare byte-for-byte.
bool Roundtrip(const wchar_t* methodName, xgzip::WriteMethod method, const std::wstring& dir) {
    std::string err;
    std::wstring zipPath = dir + L"\\" + methodName + L".zip";
    std::wstring blobPath = dir + L"\\blob.bin";
    std::string blob(100000, '\0');
    for (size_t i = 0; i < blob.size(); ++i) blob[i] = static_cast<char>((i * 7 + 11) & 0xff);
    {
        std::ofstream f(blobPath, std::ios::binary);
        f.write(blob.data(), blob.size());
    }

    const std::string repetitive = Repetitive();
    const std::string text = "xgzip ctest payload";

    {
        xgzip::WriteOptions opt;
        opt.method = method;
        opt.effort = 6;
        xgzip::Writer w(zipPath, opt);
        bool ok = w.Begin(err);
        ok = ok && w.AddDirectory(L"sub", err);
        ok = ok && w.AddMemory(L"sub\\text.txt", text.data(), text.size(), err);
        ok = ok && w.AddMemory(L"rep.bin", repetitive.data(), repetitive.size(), err);
        ok = ok && w.AddFile(L"blob.bin", blobPath, err);
        ok = ok && w.Commit(err);
        if (!ok) printf("write %ls failed: %s\n", methodName, err.c_str());
        XGT_CHECK(ok);
    }

    std::vector<xgzip::Entry> entries;
    XGT_CHECK(xgzip::List(zipPath, entries, err));
    XGT_CHECK(entries.size() == 4);   // dir entry + text + rep + blob

    std::vector<uint8_t> out;
    XGT_CHECK(xgzip::ReadEntry(zipPath, L"sub\\text.txt", out, err));
    XGT_CHECK(std::string(out.begin(), out.end()) == text);
    XGT_CHECK(xgzip::ReadEntry(zipPath, L"rep.bin", out, err));
    XGT_CHECK(std::string(out.begin(), out.end()) == repetitive);
    XGT_CHECK(xgzip::ReadEntry(zipPath, L"blob.bin", out, err));
    XGT_CHECK(std::string(out.begin(), out.end()) == blob);

    // a missing entry is an error, not an empty success
    XGT_CHECK(!xgzip::ReadEntry(zipPath, L"nope.txt", out, err));

    // path traversal is refused
    XGT_CHECK(!xgzip::ReadEntry(zipPath, L"..\\evil.txt", out, err));

    // extract to disk and verify
    std::wstring dest = dir + std::wstring(L"\\out_") + methodName;
    xgzip::Result res;
    XGT_CHECK(xgzip::Extract(zipPath, dest, xgzip::ExtractOptions{}, res, nullptr));
    XGT_CHECK(res.ok && res.files >= 3);
    XGT_CHECK(xgt::ReadAll(dest + L"\\sub\\text.txt") == text);
    XGT_CHECK(xgt::ReadAll(dest + L"\\rep.bin") == repetitive);

    if (method == xgzip::WriteMethod::Deflate) {
        for (const auto& e : entries) {
            if (e.name == L"rep.bin") {
                XGT_CHECK(e.method == 8);
                XGT_CHECK(e.compressedSize < e.size);
            }
        }
    }
    return true;
}

} // namespace

int main() {
    std::wstring dir = xgt::TempDir(L"zip");

    // crc32 of "123456789" is the classic 0xCBF43926
    XGT_CHECK(xgzip::Crc32(0, "123456789", 9) == 0xCBF43926u);

    Roundtrip(L"store", xgzip::WriteMethod::Store, dir);
    Roundtrip(L"deflate", xgzip::WriteMethod::Deflate, dir);

    xgt::DropDir(dir);
    XGT_FINISH("zip");
}
