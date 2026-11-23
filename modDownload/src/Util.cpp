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

#include "internal.h"

#include <windows.h>
#include <bcrypt.h>

#include <vector>

namespace xgdl {
namespace detail {

std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

bool EnsureParentDir(const std::wstring& filePath, std::string& err) {
    size_t slash = filePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash == 0) return true;
    std::wstring dir = filePath.substr(0, slash);

    // 逐级创建，容忍已存在
    std::wstring cur;
    for (size_t i = 0; i < dir.size(); ++i) {
        cur.push_back(dir[i]);
        bool boundary = (dir[i] == L'\\' || dir[i] == L'/') || i + 1 == dir.size();
        if (!boundary) continue;
        if (cur.size() <= 3 && cur[1] == L':') continue;   // 盘符
        if (!CreateDirectoryW(cur.c_str(), nullptr)) {
            DWORD e = GetLastError();
            if (e != ERROR_ALREADY_EXISTS) {
                err = "CreateDirectory failed: " + std::to_string(e);
                return false;
            }
        }
    }
    return true;
}

bool Sha1File(const std::wstring& path, std::wstring& hex, std::string& err) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> obj;
    std::vector<uint8_t> digest(20);
    bool ok = false;
    std::FILE* f = nullptr;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0) {
        err = "BCryptOpenAlgorithmProvider failed";
        return false;
    }
    DWORD objLen = 0, cb = 0;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0) < 0) {
        err = "BCryptGetProperty failed";
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }
    obj.resize(objLen);
    if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) < 0) {
        err = "BCryptCreateHash failed";
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    f = _wfopen(path.c_str(), L"rb");
    if (!f) {
        err = "open failed";
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    std::vector<uint8_t> buf(256 * 1024);
    size_t n = 0;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
        if (BCryptHashData(hash, buf.data(), (ULONG)n, 0) < 0) {
            err = "BCryptHashData failed";
            std::fclose(f);
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(alg, 0);
            return false;
        }
    }
    bool readErr = std::ferror(f) != 0;
    std::fclose(f);
    if (readErr) {
        err = "read failed";
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    if (BCryptFinishHash(hash, digest.data(), (ULONG)digest.size(), 0) >= 0) {
        static const wchar_t* digits = L"0123456789abcdef";
        hex.clear();
        for (uint8_t b : digest) {
            hex.push_back(digits[b >> 4]);
            hex.push_back(digits[b & 0xF]);
        }
        ok = true;
    } else {
        err = "BCryptFinishHash failed";
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

} // namespace detail
} // namespace xgdl
