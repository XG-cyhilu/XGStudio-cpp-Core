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

#include "xgfs/xgfs.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace xgfs {
namespace {

void SetErr(std::string* err, const char* what) {
    if (!err) return;
    *err = what;
    DWORD e = GetLastError();
    if (e) {
        char buf[64];
        _snprintf_s(buf, sizeof buf, _TRUNCATE, " (win32=%lu)", static_cast<unsigned long>(e));
        *err += buf;
    }
}

int64_t FileTimeToMs(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    if (!u.QuadPart) return 0;
    return static_cast<int64_t>((u.QuadPart - 116444736000000000ull) / 10000ull);
}

FILETIME MsToFileTime(int64_t ms) {
    ULARGE_INTEGER u;
    u.QuadPart = static_cast<ULONGLONG>(ms) * 10000ull + 116444736000000000ull;
    FILETIME ft;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    return ft;
}

bool MakeDirsImpl(const std::wstring& dir) {
    if (dir.empty()) return true;
    if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    std::wstring parent = dir;
    size_t pos = parent.find_last_of(L'\\');
    if (pos != std::wstring::npos && pos > 0) MakeDirsImpl(parent.substr(0, pos));
    CreateDirectoryW(dir.c_str(), nullptr);
    return GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool WildMatch(const wchar_t* pat, const wchar_t* s) {
    while (*pat) {
        if (*pat == L'*') {
            while (*pat == L'*') ++pat;
            if (!*pat) return true;
            for (const wchar_t* t = s;; ++t) {
                if (WildMatch(pat, t)) return true;
                if (!*t) return false;
            }
        }
        if (!*s) return false;
        if (*pat == L'?') {
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

bool Walk(const std::wstring& dir, const ListOptions& opt, std::vector<DirEntry>& out, std::string* err) {
    WIN32_FIND_DATAW fd{};
    std::wstring mask = dir + L"\\*";
    HANDLE h = FindFirstFileExW(mask.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        SetErr(err, "find first failed");
        return false;
    }
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        bool isDirEntry = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        std::wstring path = dir + L"\\" + fd.cFileName;
        if ((isDirEntry && opt.dirs) || (!isDirEntry && opt.files)) {
            if (!opt.pattern || WildMatch(opt.pattern, fd.cFileName)) {
                DirEntry e;
                e.name = fd.cFileName;
                e.path = path;
                e.isDir = isDirEntry;
                e.size = isDirEntry ? 0 : (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                e.mtimeMs = FileTimeToMs(fd.ftLastWriteTime);
                out.push_back(std::move(e));
            }
        }
        if (isDirEntry && opt.recursive && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            Walk(path, opt, out, err);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

} // namespace

std::wstring Normalize(const std::wstring& path) {
    if (path.empty()) {
        wchar_t cwd[MAX_PATH * 2] = {};
        GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
        return cwd;
    }
    std::wstring in = path;
    for (auto& c : in) {
        if (c == L'/') c = L'\\';
    }
    wchar_t full[MAX_PATH * 4] = {};
    DWORD n = GetFullPathNameW(in.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
    std::wstring out = (n && n < std::size(full)) ? std::wstring(full, n) : in;

    // 去掉末尾多余分隔符（但保留 "C:\"）
    while (out.size() > 1 && out.back() == L'\\' && !(out.size() == 3 && out[1] == L':')) out.pop_back();
    return out;
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return Normalize(b);
    if (b.empty()) return Normalize(a);
    if (b.size() >= 2 && b[1] == L':') return Normalize(b);   // 右值是绝对路径：直接用
    if (!b.empty() && (b[0] == L'\\' || b[0] == L'/')) return Normalize(b);
    std::wstring out = a;
    if (out.back() != L'\\' && out.back() != L'/') out += L'\\';
    out += b;
    return Normalize(out);
}

std::wstring FileName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring Extension(const std::wstring& path) {
    std::wstring name = FileName(path);
    size_t pos = name.find_last_of(L'.');
    if (pos == std::wstring::npos) return {};
    return name.substr(pos);
}

std::wstring Stem(const std::wstring& path) {
    std::wstring name = FileName(path);
    size_t pos = name.find_last_of(L'.');
    return pos == std::wstring::npos ? name : name.substr(0, pos);
}

std::wstring Parent(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return {};
    if (pos == 2 && path[1] == L':') return path.substr(0, 3);
    return path.substr(0, pos);
}

std::wstring ReplaceExtension(const std::wstring& path, const std::wstring& ext) {
    std::wstring base = path;
    std::wstring cur = Extension(base);
    if (!cur.empty()) base.resize(base.size() - cur.size());
    if (ext.empty()) return base;
    if (ext[0] != L'.') base += L'.';
    base += ext;
    return base;
}

bool Exists(const std::wstring& path) {
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool IsDir(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool IsFile(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool Size(const std::wstring& path, uint64_t& bytes) {
    WIN32_FILE_ATTRIBUTE_DATA ad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &ad)) return false;
    if (ad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    bytes = (static_cast<uint64_t>(ad.nFileSizeHigh) << 32) | ad.nFileSizeLow;
    return true;
}

bool ModifiedTime(const std::wstring& path, int64_t& ms) {
    WIN32_FILE_ATTRIBUTE_DATA ad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &ad)) return false;
    ms = FileTimeToMs(ad.ftLastWriteTime);
    return true;
}

bool SetModifiedTime(const std::wstring& path, int64_t ms) {
    FILETIME ft = MsToFileTime(ms);
    HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = SetFileTime(h, nullptr, nullptr, &ft) != 0;
    CloseHandle(h);
    return ok;
}

bool EnsureDir(const std::wstring& path, std::string* err) {
    if (!MakeDirsImpl(Normalize(path))) {
        SetErr(err, "create directory failed");
        return false;
    }
    return true;
}

bool ReadFile(const std::wstring& path, std::string& out, std::string* err) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        SetErr(err, "open for read failed");
        return false;
    }
    uint64_t size = 0;
    if (!Size(path, size)) {
        CloseHandle(h);
        SetErr(err, "stat failed");
        return false;
    }
    out.resize(static_cast<size_t>(size));
    size_t done = 0;
    while (done < out.size()) {
        DWORD want = static_cast<DWORD>(std::min<size_t>(out.size() - done, 1u << 20));
        DWORD got = 0;
        if (!::ReadFile(h, out.data() + done, want, &got, nullptr)) {
            CloseHandle(h);
            SetErr(err, "read failed");
            out.clear();
            return false;
        }
        if (!got) break;
        done += got;
    }
    CloseHandle(h);
    out.resize(done);
    return true;
}

bool ReadFile(const std::wstring& path, std::vector<uint8_t>& out, std::string* err) {
    std::string s;
    if (!ReadFile(path, s, err)) return false;
    out.assign(reinterpret_cast<const uint8_t*>(s.data()), reinterpret_cast<const uint8_t*>(s.data()) + s.size());
    return true;
}

bool WriteFile(const std::wstring& path, const void* data, size_t len, std::string* err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        SetErr(err, "open for write failed");
        return false;
    }
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t done = 0;
    bool ok = true;
    while (done < len) {
        DWORD want = static_cast<DWORD>(std::min<size_t>(len - done, 1u << 20));
        DWORD wrote = 0;
        if (!::WriteFile(h, p + done, want, &wrote, nullptr) || wrote != want) {
            ok = false;
            SetErr(err, "write failed");
            break;
        }
        done += wrote;
    }
    CloseHandle(h);
    return ok;
}

bool WriteFile(const std::wstring& path, const std::string& text, std::string* err) {
    return WriteFile(path, text.data(), text.size(), err);
}

bool WriteFileAtomic(const std::wstring& path, const void* data, size_t len, std::string* err) {
    std::wstring full = Normalize(path);
    std::wstring dir = Parent(full);
    if (!dir.empty() && !EnsureDir(dir, err)) return false;

    std::wstring tmp = full + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        SetErr(err, "create temp for atomic write failed");
        return false;
    }
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t done = 0;
    bool ok = true;
    while (done < len) {
        DWORD want = static_cast<DWORD>(std::min<size_t>(len - done, 1u << 20));
        DWORD wrote = 0;
        if (!::WriteFile(h, p + done, want, &wrote, nullptr) || wrote != want) {
            ok = false;
            SetErr(err, "atomic write data failed");
            break;
        }
        done += wrote;
    }
    if (ok) FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), full.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        SetErr(err, "atomic replace failed");
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool WriteFileAtomic(const std::wstring& path, const std::string& text, std::string* err) {
    return WriteFileAtomic(path, text.data(), text.size(), err);
}

bool Move(const std::wstring& src, const std::wstring& dst, bool overwrite, std::string* err) {
    std::wstring d = Normalize(dst);
    std::wstring parent = Parent(d);
    if (!parent.empty() && !EnsureDir(parent, err)) return false;
    DWORD flags = MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(src.c_str(), d.c_str(), flags)) {
        SetErr(err, "move failed");
        return false;
    }
    return true;
}

bool Copy(const std::wstring& src, const std::wstring& dst, bool overwrite, std::string* err) {
    std::wstring d = Normalize(dst);
    std::wstring parent = Parent(d);
    if (!parent.empty() && !EnsureDir(parent, err)) return false;
    if (!overwrite && Exists(d)) {
        SetErr(err, "destination exists");
        return false;
    }
    if (!CopyFileW(src.c_str(), d.c_str(), overwrite ? FALSE : TRUE)) {
        SetErr(err, "copy failed");
        return false;
    }
    return true;
}

bool Remove(const std::wstring& path, bool recursive, std::string* err) {
    if (!Exists(path)) return true;
    DWORD attr = GetFileAttributesW(path.c_str());
    bool isDirEntry = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);

    if (!isDirEntry) {
        if (!DeleteFileW(path.c_str())) {
            SetErr(err, "delete file failed");
            return false;
        }
        return true;
    }
    if (!recursive) {
        if (!RemoveDirectoryW(path.c_str())) {
            SetErr(err, "remove directory failed");
            return false;
        }
        return true;
    }

    // 递归：先清子项，再删自己
    WIN32_FIND_DATAW fd{};
    std::wstring mask = path + L"\\*";
    HANDLE h = FindFirstFileExW(mask.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring child = path + L"\\" + fd.cFileName;
            Remove(child, true, err);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (!RemoveDirectoryW(path.c_str())) {
        SetErr(err, "remove tree failed");
        return false;
    }
    return true;
}

bool CreateTempFile(const std::wstring& dirHint, std::wstring& path, std::string* err) {
    wchar_t dir[MAX_PATH * 2] = {};
    if (dirHint.empty()) {
        GetTempPathW(static_cast<DWORD>(std::size(dir)), dir);
    } else {
        wcsncpy_s(dir, Normalize(dirHint).c_str(), _TRUNCATE);
        if (dir[0] == 0) return false;
    }
    wchar_t name[MAX_PATH];
    if (!GetTempFileNameW(dir, L"xgt", 0, name)) {
        SetErr(err, "create temp file failed");
        return false;
    }
    path = name;
    return true;
}

bool ListDir(const std::wstring& dir, std::vector<DirEntry>& out, const ListOptions& opt, std::string* err) {
    if (!IsDir(dir)) {
        SetErr(err, "not a directory");
        return false;
    }
    return Walk(Normalize(dir), opt, out, err);
}

} // namespace xgfs
