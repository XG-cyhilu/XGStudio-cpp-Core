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

#include "inflate.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace xgzip {
namespace {

constexpr uint32_t kSigLocal = 0x04034b50;   // PK\3\4
constexpr uint32_t kSigCentral = 0x02014b50;
constexpr uint32_t kSigEocd = 0x06054b50;
constexpr uint32_t kSigZip64Eocd = 0x06064b50;
constexpr uint32_t kSigZip64Locator = 0x07064b50;

uint16_t Rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t Rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t Rd64(const uint8_t* p) {
    return static_cast<uint64_t>(Rd32(p)) | (static_cast<uint64_t>(Rd32(p + 4)) << 32);
}

struct File {
    FILE* f = nullptr;
    ~File() {
        if (f) fclose(f);
    }
    bool Open(const std::wstring& path) {
        return _wfopen_s(&f, path.c_str(), L"rb") == 0 && f;
    }
    bool Seek(int64_t off) { return _fseeki64(f, off, SEEK_SET) == 0; }
    bool Read(void* dst, size_t len) { return len == 0 || fread(dst, 1, len, f) == len; }
    int64_t Size() {
        int64_t cur = _ftelli64(f);
        _fseeki64(f, 0, SEEK_END);
        int64_t n = _ftelli64(f);
        _fseeki64(f, cur, SEEK_SET);
        return n;
    }
};

// 从归档里 [offset, offset+len) 读压缩数据
class ArchiveSource : public detail::ByteSource {
public:
    ArchiveSource(FILE* f, int64_t offset, uint64_t len) : f_(f), next_(offset), left_(len) {}

    bool Read(uint8_t* dst, size_t want, size_t& got) override {
        got = 0;
        if (!left_) return true;
        size_t n = static_cast<size_t>(std::min<uint64_t>(left_, want));
        if (_fseeki64(f_, next_, SEEK_SET) != 0) return false;
        size_t rd = fread(dst, 1, n, f_);
        if (rd != n) return false;
        next_ += static_cast<int64_t>(rd);
        left_ -= rd;
        got = rd;
        return true;
    }

private:
    FILE* f_;
    int64_t next_;
    uint64_t left_;
};

class FileSink : public detail::ByteSink {
public:
    explicit FileSink(FILE* f) : f_(f) {}

    bool Write(const uint8_t* src, size_t len) override {
        if (len && fwrite(src, 1, len, f_) != len) return false;
        bytes_ += len;
        crc_ = Crc32(crc_, src, len);
        return true;
    }

    uint64_t bytes() const { return bytes_; }
    uint32_t crc() const { return crc_; }

private:
    FILE* f_;
    uint64_t bytes_ = 0;
    uint32_t crc_ = 0;
};

class MemorySink : public detail::ByteSink {
public:
    bool Write(const uint8_t* src, size_t len) override {
        buf.insert(buf.end(), src, src + len);
        crc_ = Crc32(crc_, src, len);
        return true;
    }

    std::vector<uint8_t> buf;
    uint32_t crc_ = 0;
};

// 去掉 '/' 分隔、拒绝越界路径
bool Sanitize(std::wstring name, std::wstring& out) {
    std::replace(name.begin(), name.end(), L'/', L'\\');
    while (!name.empty() && name.back() == L'\\') name.pop_back();
    if (name.empty()) return false;

    if (name.size() >= 2 && name[1] == L':') return false;   // 带盘符
    if (name[0] == L'\\') return false;                      // 绝对路径

    std::wstring acc;
    size_t start = 0;
    while (start <= name.size()) {
        size_t sep = name.find(L'\\', start);
        if (sep == std::wstring::npos) sep = name.size();
        std::wstring part = name.substr(start, sep - start);
        if (part == L".." ) return false;
        if (part == L"." || part.empty()) {
            // 跳过
        } else {
            if (!acc.empty()) acc += L'\\';
            acc += part;
        }
        start = sep + 1;
    }
    if (acc.empty()) return false;
    out = acc;
    return true;
}

bool WildMatch(const wchar_t* pat, const wchar_t* s) {
    while (*pat) {
        if (*pat == L'*') {
            while (*pat == L'*') ++pat;
            if (!*pat) return true;
            for (const wchar_t* t = s; ; ++t) {
                if (WildMatch(pat, t)) return true;
                if (!*t) return false;
            }
        }
        if (!*s) return false;
        if (*pat == L'?' ) {
            ++pat;
            ++s;
            continue;
        }
        wchar_t a = (*pat >= L'A' && *pat <= L'Z') ? static_cast<wchar_t>(*pat + 32) : *pat;
        wchar_t b = (*s >= L'A' && *s <= L'Z') ? static_cast<wchar_t>(*s + 32) : *s;
        if (a != b) return false;
        ++pat;
        ++s;
    }
    return *s == 0;
}

bool Matches(const ExtractOptions& opt, const std::wstring& rel) {
    if (opt.filter.empty()) return true;
    std::wstring f = opt.filter;
    std::replace(f.begin(), f.end(), L'/', L'\\');
    if (!f.empty() && f.back() == L'\\') {
        std::wstring prefix = f;
        if (rel.size() >= prefix.size()) {
            std::wstring head = rel.substr(0, prefix.size());
            return _wcsicmp(head.c_str(), prefix.c_str()) == 0;
        }
        return false;
    }
    return WildMatch(f.c_str(), rel.c_str());
}

bool MakeDirs(const std::wstring& dir) {
    if (dir.empty()) return true;
    if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    std::wstring parent = dir;
    size_t pos = parent.find_last_of(L'\\');
    if (pos != std::wstring::npos && pos > 0) MakeDirs(parent.substr(0, pos));
    CreateDirectoryW(dir.c_str(), nullptr);
    return GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES;
}

struct Archive {
    File file;
    std::vector<Entry> entries;
    std::vector<int64_t> localOffsets;
};

bool ParseExtraZip64(const uint8_t* extra, size_t len, uint64_t& usize, uint64_t& csize, uint64_t& offset,
                     bool& hasUsize, bool& hasCsize, bool& hasOffset) {
    size_t i = 0;
    while (i + 4 <= len) {
        uint16_t id = Rd16(extra + i);
        uint16_t sz = Rd16(extra + i + 2);
        if (i + 4 + sz > len) break;
        if (id == 0x0001) {
            size_t p = i + 4;
            auto take = [&](uint64_t& dst) -> bool {
                if (p + 8 > i + 4 + sz) return false;
                dst = Rd64(extra + p);
                p += 8;
                return true;
            };
            if (hasUsize && !take(usize)) return false;
            if (hasCsize && !take(csize)) return false;
            if (hasOffset && !take(offset)) return false;
            return true;
        }
        i += 4 + sz;
    }
    return true;
}

bool OpenArchive(const std::wstring& path, Archive& a, std::string& err) {
    if (!a.file.Open(path)) {
        err = "cannot open zip";
        return false;
    }
    int64_t size = a.file.Size();
    if (size < 22) {
        err = "file too small for zip";
        return false;
    }

    // 定位 EOCD：注释最长 65535，所以从尾部最多看 65557 字节
    int64_t tail = std::min<int64_t>(size, 65557);
    std::vector<uint8_t> buf(static_cast<size_t>(tail));
    if (!a.file.Seek(size - tail) || !a.file.Read(buf.data(), buf.size())) {
        err = "read tail failed";
        return false;
    }
    int64_t eocdPos = -1;
    for (int64_t i = tail - 22; i >= 0; --i) {
        if (Rd32(buf.data() + i) == kSigEocd) {
            uint16_t comment = Rd16(buf.data() + i + 20);
            if (i + 22 + comment == tail) {
                eocdPos = i;
                break;
            }
        }
    }
    if (eocdPos < 0) {
        err = "end of central directory not found";
        return false;
    }

    const uint8_t* e = buf.data() + eocdPos;
    uint16_t disk = Rd16(e + 4);
    uint16_t cdDisk = Rd16(e + 6);
    uint16_t countDisk = Rd16(e + 8);
    uint16_t count = Rd16(e + 10);
    uint64_t cdSize = Rd32(e + 12);
    uint64_t cdOffset = Rd32(e + 16);

    uint64_t realCount = count;
    bool zip64 = (count == 0xFFFF || cdOffset == 0xFFFFFFFF || cdSize == 0xFFFFFFFF);
    if (zip64) {
        int64_t locPos = size - tail + eocdPos - 20;
        if (locPos >= 0) {
            std::vector<uint8_t> loc(20);
            if (a.file.Seek(locPos) && a.file.Read(loc.data(), loc.size()) && Rd32(loc.data()) == kSigZip64Locator) {
                uint64_t z64Off = Rd64(loc.data() + 8);
                std::vector<uint8_t> z64(56);
                if (a.file.Seek(static_cast<int64_t>(z64Off)) && a.file.Read(z64.data(), z64.size()) &&
                    Rd32(z64.data()) == kSigZip64Eocd) {
                    realCount = Rd64(z64.data() + 32);
                    cdSize = Rd64(z64.data() + 40);
                    cdOffset = Rd64(z64.data() + 48);
                    disk = 0;
                }
            }
        }
    }
    if (disk != 0 || cdDisk != 0) {
        err = "multi-disk zip not supported";
        return false;
    }
    (void)countDisk;

    a.entries.reserve(static_cast<size_t>(realCount));
    a.localOffsets.reserve(static_cast<size_t>(realCount));

    if (!a.file.Seek(static_cast<int64_t>(cdOffset))) {
        err = "seek central directory failed";
        return false;
    }
    for (uint64_t i = 0; i < realCount; ++i) {
        uint8_t h[46];
        if (!a.file.Read(h, sizeof h)) {
            err = "truncated central directory";
            return false;
        }
        if (Rd32(h) != kSigCentral) {
            err = "bad central directory signature";
            return false;
        }
        uint16_t flags = Rd16(h + 8);
        uint16_t method = Rd16(h + 10);
        uint32_t crc = Rd32(h + 16);
        uint64_t csize = Rd32(h + 20);
        uint64_t usize = Rd32(h + 24);
        uint16_t nameLen = Rd16(h + 28);
        uint16_t extraLen = Rd16(h + 30);
        uint16_t commentLen = Rd16(h + 32);
        uint64_t localOff = Rd32(h + 42);

        std::string rawName(static_cast<size_t>(nameLen), '\0');
        if (nameLen && !a.file.Read(rawName.data(), nameLen)) {
            err = "truncated entry name";
            return false;
        }
        std::vector<uint8_t> extra(extraLen);
        if (extraLen && !a.file.Read(extra.data(), extraLen)) {
            err = "truncated extra field";
            return false;
        }
        if (commentLen) {
            std::vector<uint8_t> skip(commentLen);
            if (!a.file.Read(skip.data(), skip.size())) {
                err = "truncated comment";
                return false;
            }
        }

        bool hasUsize = (usize == 0xFFFFFFFF);
        bool hasCsize = (csize == 0xFFFFFFFF);
        bool hasOffset = (localOff == 0xFFFFFFFF);
        if (hasUsize || hasCsize || hasOffset) {
            if (!ParseExtraZip64(extra.data(), extra.size(), usize, csize, localOff, hasUsize, hasCsize, hasOffset)) {
                err = "bad zip64 extra field";
                return false;
            }
        }

        Entry en;
        en.size = usize;
        en.compressedSize = csize;
        en.crc32 = crc;
        en.method = method;
        en.encrypted = (flags & 1) != 0;
        en.isDir = !rawName.empty() && (rawName.back() == '/' || rawName.back() == '\\');
        // 名子里 0x0800 位表示 UTF-8；否则按当前 ANSI 页解释（中文名在资源管理器里压出来的包就属于这种）
        int cp = (flags & 0x0800) ? CP_UTF8 : GetACP();
        int need = MultiByteToWideChar(cp, 0, rawName.c_str(), static_cast<int>(rawName.size()), nullptr, 0);
        std::wstring w(static_cast<size_t>(need > 0 ? need : 0), L'\0');
        if (need > 0)
            MultiByteToWideChar(cp, 0, rawName.c_str(), static_cast<int>(rawName.size()), w.data(), need);
        en.name = w;
        std::wstring rel;
        if (!Sanitize(w, rel)) rel.clear();
        en.name = rel;

        a.entries.push_back(en);
        a.localOffsets.push_back(static_cast<int64_t>(localOff));
    }
    return true;
}

// 取条目数据段起点；本地头的 name/extra 长度可能和中央目录不一致，必须单独读
bool DataStart(Archive& a, size_t idx, int64_t& start, std::string& err) {
    if (!a.file.Seek(a.localOffsets[idx])) {
        err = "seek local header failed";
        return false;
    }
    uint8_t h[30];
    if (!a.file.Read(h, sizeof h) || Rd32(h) != kSigLocal) {
        err = "bad local header";
        return false;
    }
    uint16_t nameLen = Rd16(h + 26);
    uint16_t extraLen = Rd16(h + 28);
    start = a.localOffsets[idx] + 30 + nameLen + extraLen;
    return true;
}

bool InflateEntry(Archive& a, size_t idx, detail::ByteSink& sink, std::string& err) {
    int64_t start = 0;
    if (!DataStart(a, idx, start, err)) return false;
    const Entry& en = a.entries[idx];
    if (en.encrypted) {
        err = "encrypted entry not supported";
        return false;
    }
    if (en.size == en.compressedSize && en.method == 0) {
        // store：直接抄
        ArchiveSource src(a.file.f, start, en.compressedSize);
        uint8_t chunk[16384];
        while (true) {
            size_t got = 0;
            if (!src.Read(chunk, sizeof chunk, got)) {
                err = "read stored entry failed";
                return false;
            }
            if (!got) break;
            if (!sink.Write(chunk, got)) {
                err = "write failed";
                return false;
            }
        }
        return true;
    }
    if (en.method == 0) {
        // 少见：method=0 但大小不一致，说明中央目录可疑
        err = "store entry with inconsistent sizes";
        return false;
    }
    if (en.method != 8) {
        err = "unsupported compression method";
        return false;
    }
    ArchiveSource src(a.file.f, start, en.compressedSize);
    if (!detail::Inflate(src, sink, err)) return false;
    return true;
}

} // namespace

uint32_t Crc32(uint32_t seed, const void* data, size_t len) {
    static const std::array<uint32_t, 256> tab = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();

    uint32_t c = seed ^ 0xFFFFFFFFu;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) c = tab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool List(const std::wstring& zipPath, std::vector<Entry>& out, std::string& err) {
    Archive a;
    if (!OpenArchive(zipPath, a, err)) return false;
    out = a.entries;
    return true;
}

bool Extract(const std::wstring& zipPath, const std::wstring& destDir, const ExtractOptions& opt, Result& res,
             Stats* stats) {
    const auto t0 = std::chrono::steady_clock::now();
    res = Result{};
    if (stats) stats->Reset();

    std::string err;
    Archive a;
    if (!OpenArchive(zipPath, a, err)) {
        res.error = err;
        res.ok = false;
        return false;
    }

    std::wstring dest = destDir;
    if (!dest.empty() && dest.back() != L'\\') dest += L'\\';
    if (!MakeDirs(destDir)) {
        res.error = "cannot create destination directory";
        return false;
    }

    uint64_t totalBytes = 0;
    uint32_t totalFiles = 0;
    for (const auto& en : a.entries) {
        if (en.isDir || en.name.empty() || !Matches(opt, en.name)) continue;
        totalBytes += en.size;
        ++totalFiles;
    }
    if (stats) {
        stats->bytesTotal = totalBytes;
        stats->filesTotal = totalFiles;
    }

    bool anyFail = false;
    for (size_t i = 0; i < a.entries.size(); ++i) {
        const Entry& en = a.entries[i];
        if (en.name.empty()) {
            anyFail = true;
            res.failed.push_back(L"<path rejected>");
            if (res.error.empty()) res.error = "entry path rejected";
            continue;
        }
        if (en.isDir) {
            if (!Matches(opt, en.name)) continue;
            std::wstring d = dest + en.name;
            if (opt.keepPaths) {
                if (MakeDirs(d)) ++res.dirs;
            }
            continue;
        }
        if (!Matches(opt, en.name)) continue;

        std::wstring rel = en.name;
        if (!opt.keepPaths) {
            size_t pos = rel.find_last_of(L'\\');
            rel = pos == std::wstring::npos ? rel : rel.substr(pos + 1);
        }
        std::wstring full = dest + rel;
        size_t pos = full.find_last_of(L'\\');
        if (pos != std::wstring::npos && !MakeDirs(full.substr(0, pos))) {
            anyFail = true;
            res.failed.push_back(en.name);
            if (res.error.empty()) res.error = "cannot create directory for entry";
            continue;
        }

        if (!opt.overwrite && GetFileAttributesW(full.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ++res.skipped;
            continue;
        }

        FILE* f = nullptr;
        if (_wfopen_s(&f, full.c_str(), L"wb") != 0 || !f) {
            anyFail = true;
            res.failed.push_back(en.name);
            if (res.error.empty()) res.error = "cannot create target file";
            continue;
        }

        FileSink sink(f);
        std::string ierr;
        bool ok = InflateEntry(a, i, sink, ierr);
        fclose(f);

        if (ok) {
            if (sink.bytes() != en.size) {
                ok = false;
                ierr = "size mismatch";
            } else if (sink.crc() != en.crc32) {
                ok = false;
                ierr = "crc mismatch";
            }
        }
        if (!ok) {
            DeleteFileW(full.c_str());
            anyFail = true;
            res.failed.push_back(en.name);
            if (res.error.empty()) res.error = ierr.empty() ? "extract failed" : ierr;
            continue;
        }

        ++res.files;
        res.bytes += en.size;
        if (stats) {
            stats->bytesDone.fetch_add(en.size);
            stats->filesDone.fetch_add(1);
        }
    }

    res.ok = !anyFail;
    res.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    return res.ok;
}

bool ReadEntry(const std::wstring& zipPath, const std::wstring& name, std::vector<uint8_t>& out, std::string& err) {
    Archive a;
    if (!OpenArchive(zipPath, a, err)) return false;

    std::wstring rel;
    if (!Sanitize(name, rel)) {
        err = "invalid entry name";
        return false;
    }
    for (size_t i = 0; i < a.entries.size(); ++i) {
        if (_wcsicmp(a.entries[i].name.c_str(), rel.c_str()) != 0) continue;
        MemorySink sink;
        if (!InflateEntry(a, i, sink, err)) return false;
        if (sink.buf.size() != a.entries[i].size || sink.crc_ != a.entries[i].crc32) {
            err = "entry data mismatch";
            return false;
        }
        out = std::move(sink.buf);
        return true;
    }
    err = "entry not found";
    return false;
}

} // namespace xgzip
