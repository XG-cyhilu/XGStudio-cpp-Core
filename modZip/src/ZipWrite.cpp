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

#include "xgzip/xgzip.h"

#include "deflate.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace xgzip {

namespace {

constexpr uint32_t kSigLocal = 0x04034b50;
constexpr uint32_t kSigCentral = 0x02014b50;
constexpr uint32_t kSigEocd = 0x06054b50;
constexpr uint16_t kFlagUtf8 = 0x0800;
constexpr uint32_t kMax32 = 0xFFFFFFFFu;
constexpr uint16_t kMaxEntries = 0xFFFF;

void Wr16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)(v >> 8));
}

void Wr32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)((v >> 8) & 0xFF));
    b.push_back((uint8_t)((v >> 16) & 0xFF));
    b.push_back((uint8_t)((v >> 24) & 0xFF));
}

std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

bool IsAscii(const std::string& s) {
    for (unsigned char c : s)
        if (c >= 0x80) return false;
    return true;
}

// 条目名规范化：反斜杠换成正斜杠，去掉前导 '/'，拒绝盘符与 ".."
bool NormalizeName(const std::wstring& in, std::wstring& out, std::string& err) {
    std::wstring s = in;
    for (auto& c : s) if (c == L'\\') c = L'/';
    while (!s.empty() && s[0] == L'/') s.erase(s.begin());
    if (s.size() >= 2 && s[1] == L':') { err = "absolute path rejected"; return false; }
    // 逐段检查
    size_t start = 0;
    while (start <= s.size()) {
        size_t slash = s.find(L'/', start);
        if (slash == std::wstring::npos) slash = s.size();
        std::wstring seg = s.substr(start, slash - start);
        if (seg == L"..") { err = "'..' rejected"; return false; }
        start = slash + 1;
        if (slash == s.size()) break;
    }
    if (s.empty()) { err = "empty entry name"; return false; }
    out = s;
    return true;
}

void DosTime(const FILETIME* ft, uint16_t& date, uint16_t& time) {
    FILETIME local = {};
    if (ft) FileTimeToLocalFileTime(ft, &local);
    SYSTEMTIME st{};
    if (ft) FileTimeToSystemTime(&local, &st);
    else GetLocalTime(&st);
    if (st.wYear < 1980) st.wYear = 1980;
    date = (uint16_t)(((st.wYear - 1980) & 0x7F) << 9 | (st.wMonth & 0x0F) << 5 | (st.wDay & 0x1F));
    time = (uint16_t)((st.wHour & 0x1F) << 11 | (st.wMinute & 0x3F) << 5 | (st.wSecond / 2) & 0x1F);
}

struct Item {
    std::string name;       // UTF-8
    uint16_t flags = 0;
    uint16_t method = 0;
    uint16_t date = 0, time = 0;
    uint32_t crc = 0;
    uint64_t csize = 0, usize = 0;
    uint64_t offset = 0;
};

} // namespace

struct Writer::Impl {
    std::wstring path, tmpPath;
    WriteOptions opt;
    std::FILE* f = nullptr;
    uint64_t offset = 0;
    std::vector<Item> items;
    bool committed = false;

    bool WriteBytes(const void* p, size_t n) {
        if (!n) return true;
        if (std::fwrite(p, 1, n, f) != n) return false;
        offset += n;
        return true;
    }

    bool WriteVec(const std::vector<uint8_t>& v) {
        return WriteBytes(v.data(), v.size());
    }

    bool AddEntry(const std::wstring& rawName, const void* data, size_t len, bool dir,
                  const FILETIME* ft, std::string& err);
};

bool Writer::Impl::AddEntry(const std::wstring& rawName, const void* data, size_t len, bool dir,
                            const FILETIME* ft, std::string& err) {
    std::wstring norm;
    if (!NormalizeName(rawName, norm, err)) return false;
    std::string name = Utf8(norm);
    if (name.empty()) { err = "empty entry name"; return false; }

    Item it;
    it.name = name;
    it.flags = (opt.utf8Names && !IsAscii(name)) ? kFlagUtf8 : 0;
    it.usize = dir ? 0 : len;
    it.method = 0;
    uint16_t date = 0, time = 0;
    DosTime(ft, date, time);
    it.date = date;
    it.time = time;

    std::vector<uint8_t> compressed;
    if (!dir && opt.method == WriteMethod::Deflate && len) {
        if (!detail::DeflateFixed((const uint8_t*)data, len, compressed, opt.effort)) {
            err = "deflate failed";
            return false;
        }
        // 压不小就别压了：store 更省解压时间
        if (compressed.size() < len) {
            it.method = 8;
            it.csize = compressed.size();
        } else {
            compressed.clear();
            it.csize = len;
        }
    } else {
        it.csize = dir ? 0 : len;
    }

    const uint8_t* body = compressed.empty() ? (const uint8_t*)data : compressed.data();
    it.crc = dir ? 0 : Crc32(0, data, len);

    if (it.usize > kMax32 || it.csize > kMax32 || offset > kMax32) {
        err = "zip64 required (entry over 4GB) — writer only emits classic zip";
        return false;
    }

    it.offset = offset;
    std::vector<uint8_t> h;
    Wr32(h, kSigLocal);
    Wr16(h, it.method == 8 ? 20 : 10);
    Wr16(h, it.flags);
    Wr16(h, it.method);
    Wr16(h, it.time);
    Wr16(h, it.date);
    Wr32(h, it.crc);
    Wr32(h, (uint32_t)it.csize);
    Wr32(h, (uint32_t)it.usize);
    Wr16(h, (uint16_t)name.size());
    Wr16(h, 0);   // extra
    if (!WriteVec(h)) { err = "write failed"; return false; }
    if (!WriteBytes(name.data(), name.size())) { err = "write failed"; return false; }
    if (it.csize && !WriteBytes(body, (size_t)it.csize)) { err = "write failed"; return false; }

    items.push_back(it);
    return true;
}

Writer::Writer(const std::wstring& zipPath, const WriteOptions& opt)
    : impl_(new Impl()) {
    impl_->path = zipPath;
    impl_->tmpPath = zipPath + L".tmp";
    impl_->opt = opt;
}

Writer::~Writer() {
    if (impl_->f) {
        std::fclose(impl_->f);
        impl_->f = nullptr;
    }
    if (!impl_->committed) DeleteFileW(impl_->tmpPath.c_str());
}

bool Writer::Begin(std::string& err) {
    size_t slash = impl_->path.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash > 2) {
        std::wstring dir = impl_->path.substr(0, slash);
        std::wstring cur;
        for (size_t i = 0; i < dir.size(); ++i) {
            cur.push_back(dir[i]);
            bool boundary = dir[i] == L'\\' || dir[i] == L'/' || i + 1 == dir.size();
            if (!boundary) continue;
            if (cur.size() <= 3 && cur[1] == L':') continue;
            CreateDirectoryW(cur.c_str(), nullptr);
        }
    }
    _wfopen_s(&impl_->f, impl_->tmpPath.c_str(), L"wb");
    if (!impl_->f) { err = "cannot create temp file"; return false; }
    impl_->offset = 0;
    impl_->items.clear();
    return true;
}

bool Writer::AddFile(const std::wstring& archiveName, const std::wstring& diskPath,
                     std::string& err) {
    if (!impl_->f) { err = "Begin() not called"; return false; }
    WIN32_FILE_ATTRIBUTE_DATA ad{};
    if (!GetFileAttributesExW(diskPath.c_str(), GetFileExInfoStandard, &ad)) {
        err = "cannot stat " + Utf8(diskPath);
        return false;
    }
    if (ad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        err = "is a directory";
        return false;
    }
    uint64_t size = ((uint64_t)ad.nFileSizeHigh << 32) | ad.nFileSizeLow;

    std::FILE* src = nullptr;
    _wfopen_s(&src, diskPath.c_str(), L"rb");
    if (!src) { err = "cannot open " + Utf8(diskPath); return false; }

    if (impl_->opt.method == WriteMethod::Deflate) {
        std::vector<uint8_t> all((size_t)size);
        size_t got = all.empty() ? 0 : std::fread(all.data(), 1, (size_t)size, src);
        std::fclose(src);
        if (got != size) { err = "short read"; return false; }
        return impl_->AddEntry(archiveName, all.data(), all.size(), false,
                               impl_->opt.preserveMtime ? &ad.ftLastWriteTime : nullptr, err);
    }

    // Store：先算 CRC，再流式写。避免整文件驻留内存。
    uint32_t crc = 0;
    {
        std::vector<uint8_t> buf(256 * 1024);
        size_t n = 0;
        while ((n = std::fread(buf.data(), 1, buf.size(), src)) > 0) crc = Crc32(crc, buf.data(), n);
        if (std::ferror(src)) { std::fclose(src); err = "read failed"; return false; }
        _fseeki64(src, 0, SEEK_SET);
    }

    std::wstring norm;
    if (!NormalizeName(archiveName, norm, err)) { std::fclose(src); return false; }
    std::string name = Utf8(norm);
    Item it;
    it.name = name;
    it.flags = (impl_->opt.utf8Names && !IsAscii(name)) ? kFlagUtf8 : 0;
    it.method = 0;
    it.crc = crc;
    it.usize = it.csize = size;
    DosTime(impl_->opt.preserveMtime ? &ad.ftLastWriteTime : nullptr, it.date, it.time);
    if (size > kMax32 || impl_->offset > kMax32) {
        std::fclose(src);
        err = "zip64 required (entry over 4GB) — writer only emits classic zip";
        return false;
    }
    it.offset = impl_->offset;

    std::vector<uint8_t> h;
    Wr32(h, kSigLocal);
    Wr16(h, 10);
    Wr16(h, it.flags);
    Wr16(h, 0);
    Wr16(h, it.time);
    Wr16(h, it.date);
    Wr32(h, crc);
    Wr32(h, (uint32_t)size);
    Wr32(h, (uint32_t)size);
    Wr16(h, (uint16_t)name.size());
    Wr16(h, 0);
    bool ok = impl_->WriteVec(h) && impl_->WriteBytes(name.data(), name.size());

    std::vector<uint8_t> buf(256 * 1024);
    size_t n = 0;
    uint64_t written = 0;
    while (ok && (n = std::fread(buf.data(), 1, buf.size(), src)) > 0) {
        ok = impl_->WriteBytes(buf.data(), n);
        written += n;
    }
    std::fclose(src);
    if (!ok || written != size) { err = "write failed"; return false; }
    impl_->items.push_back(it);
    return true;
}

bool Writer::AddMemory(const std::wstring& archiveName, const void* data, size_t len,
                       std::string& err) {
    if (!impl_->f) { err = "Begin() not called"; return false; }
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return impl_->AddEntry(archiveName, data, len, false,
                           impl_->opt.preserveMtime ? &now : nullptr, err);
}

bool Writer::AddDirectory(const std::wstring& archiveName, std::string& err) {
    if (!impl_->f) { err = "Begin() not called"; return false; }
    std::wstring name = archiveName;
    if (name.empty() || (name.back() != L'/' && name.back() != L'\\')) name.push_back(L'/');
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return impl_->AddEntry(name, nullptr, 0, true, impl_->opt.preserveMtime ? &now : nullptr, err);
}

bool Writer::Commit(std::string& err) {
    Impl& s = *impl_;
    if (!s.f) { err = "Begin() not called"; return false; }
    if (s.items.size() > kMaxEntries) { err = "too many entries for classic zip"; return false; }
    uint64_t centralStart = s.offset;

    for (auto& it : s.items) {
        std::vector<uint8_t> h;
        Wr32(h, kSigCentral);
        Wr16(h, 20);                       // version made by (DOS)
        Wr16(h, it.method == 8 ? 20 : 10); // version needed
        Wr16(h, it.flags);
        Wr16(h, it.method);
        Wr16(h, it.time);
        Wr16(h, it.date);
        Wr32(h, it.crc);
        Wr32(h, (uint32_t)it.csize);
        Wr32(h, (uint32_t)it.usize);
        Wr16(h, (uint16_t)it.name.size());
        Wr16(h, 0);   // extra
        Wr16(h, 0);   // comment
        Wr16(h, 0);   // disk start
        Wr16(h, 0);   // internal attrs
        Wr32(h, 0);   // external attrs
        Wr32(h, (uint32_t)it.offset);
        if (!s.WriteVec(h) || !s.WriteBytes(it.name.data(), it.name.size())) {
            err = "write failed";
            return false;
        }
    }
    uint64_t centralSize = s.offset - centralStart;
    if (s.offset > kMax32 || centralSize > kMax32) {
        err = "zip64 required (archive over 4GB)";
        return false;
    }

    std::vector<uint8_t> e;
    Wr32(e, kSigEocd);
    Wr16(e, 0);
    Wr16(e, 0);
    Wr16(e, (uint16_t)s.items.size());
    Wr16(e, (uint16_t)s.items.size());
    Wr32(e, (uint32_t)centralSize);
    Wr32(e, (uint32_t)centralStart);
    Wr16(e, 0);
    if (!s.WriteVec(e)) { err = "write failed"; return false; }

    std::fflush(s.f);
    std::fclose(s.f);
    s.f = nullptr;

    if (!MoveFileExW(s.tmpPath.c_str(), s.path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        err = "rename failed: " + std::to_string(GetLastError());
        return false;
    }
    s.committed = true;
    return true;
}

void Writer::Abort() {
    if (impl_->f) {
        std::fclose(impl_->f);
        impl_->f = nullptr;
    }
    DeleteFileW(impl_->tmpPath.c_str());
}

uint64_t Writer::BytesWritten() const { return impl_->offset; }

uint32_t Writer::EntriesWritten() const { return (uint32_t)impl_->items.size(); }

} // namespace xgzip
