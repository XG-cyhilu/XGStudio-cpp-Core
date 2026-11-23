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

#include "xgcrypto/xgcrypto.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstring>

#pragma comment(lib, "bcrypt.lib")

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace xgcrypto {
namespace {

const wchar_t* AlgId(Alg a) {
    switch (a) {
        case Alg::Md5: return BCRYPT_MD5_ALGORITHM;
        case Alg::Sha1: return BCRYPT_SHA1_ALGORITHM;
        case Alg::Sha256: return BCRYPT_SHA256_ALGORITHM;
        case Alg::Sha384: return BCRYPT_SHA384_ALGORITHM;
        case Alg::Sha512: return BCRYPT_SHA512_ALGORITHM;
    }
    return BCRYPT_SHA256_ALGORITHM;
}

struct Provider {
    BCRYPT_ALG_HANDLE h = nullptr;
    uint32_t hashLen = 0;
    uint32_t objLen = 0;

    bool Open(Alg a) {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&h, AlgId(a), nullptr, 0))) {
            h = nullptr;
            return false;
        }
        ULONG cb = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(h, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen),
                                             sizeof objLen, &cb, 0)) ||
            !BCRYPT_SUCCESS(BCryptGetProperty(h, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen),
                                              sizeof hashLen, &cb, 0))) {
            Close();
            return false;
        }
        return true;
    }

    void Close() {
        if (h) {
            BCryptCloseAlgorithmProvider(h, 0);
            h = nullptr;
        }
    }
};

} // namespace

uint32_t DigestSize(Alg a) {
    switch (a) {
        case Alg::Md5: return 16;
        case Alg::Sha1: return 20;
        case Alg::Sha256: return 32;
        case Alg::Sha384: return 48;
        case Alg::Sha512: return 64;
    }
    return 0;
}

const char* AlgName(Alg a) {
    switch (a) {
        case Alg::Md5: return "md5";
        case Alg::Sha1: return "sha1";
        case Alg::Sha256: return "sha256";
        case Alg::Sha384: return "sha384";
        case Alg::Sha512: return "sha512";
    }
    return "?";
}

struct Digest::Impl {
    Alg alg = Alg::Sha256;
    Provider prov;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> obj;
    std::vector<uint8_t> done;   // Final 之后的缓存
    bool failed = false;

    bool Create() {
        if (!prov.Open(alg)) {
            failed = true;
            return false;
        }
        obj.assign(prov.objLen, 0);
        if (!BCRYPT_SUCCESS(BCryptCreateHash(prov.h, &hash, obj.data(), prov.objLen, nullptr, 0, 0))) {
            hash = nullptr;
            failed = true;
            return false;
        }
        return true;
    }

    void Release() {
        if (hash) {
            BCryptDestroyHash(hash);
            hash = nullptr;
        }
        prov.Close();
    }
};

Digest::Digest(Alg a) : impl_(new Impl) {
    impl_->alg = a;
}

Digest::~Digest() {
    impl_->Release();
}

bool Digest::Update(const void* data, size_t len) {
    if (impl_->failed) return false;
    if (!impl_->hash && !impl_->done.empty()) return false;   // 已 Final
    if (!impl_->hash && !impl_->Create()) return false;
    if (!len) return true;
    if (!BCRYPT_SUCCESS(BCryptHashData(impl_->hash, const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),
                                       static_cast<ULONG>(len), 0))) {
        impl_->failed = true;
        return false;
    }
    return true;
}

bool Digest::Final(std::vector<uint8_t>& out) {
    if (!impl_->done.empty()) {
        out = impl_->done;
        return true;
    }
    if (impl_->failed) return false;
    if (!impl_->hash && !impl_->Create()) return false;
    std::vector<uint8_t> buf(impl_->prov.hashLen);
    if (!BCRYPT_SUCCESS(BCryptFinishHash(impl_->hash, buf.data(), static_cast<ULONG>(buf.size()), 0))) {
        impl_->failed = true;
        return false;
    }
    impl_->done = buf;
    out = std::move(buf);
    return true;
}

std::vector<uint8_t> Hash(Alg a, const void* data, size_t len) {
    Digest d(a);
    std::vector<uint8_t> out;
    if (!d.Update(data, len)) return {};
    if (!d.Final(out)) return {};
    return out;
}

std::vector<uint8_t> HashFile(Alg a, const std::wstring& path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return {};
    Digest d(a);
    std::vector<uint8_t> chunk(256 * 1024);
    bool ok = true;
    while (true) {
        size_t n = fread(chunk.data(), 1, chunk.size(), f);
        if (n && !d.Update(chunk.data(), n)) { ok = false; break; }
        if (n < chunk.size()) break;
    }
    fclose(f);
    std::vector<uint8_t> out;
    if (!ok || !d.Final(out)) return {};
    return out;
}

std::string ToHex(const std::vector<uint8_t>& bytes) {
    static const char* kHex = "0123456789abcdef";
    std::string s;
    s.resize(bytes.size() * 2);
    for (size_t i = 0; i < bytes.size(); ++i) {
        s[i * 2] = kHex[bytes[i] >> 4];
        s[i * 2 + 1] = kHex[bytes[i] & 0xF];
    }
    return s;
}

std::vector<uint8_t> FromHex(const std::string& hex) {
    if (hex.size() % 2) return {};
    std::vector<uint8_t> out;
    out.resize(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = nib(hex[i * 2]), lo = nib(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return {};
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return out;
}

std::string HashHex(Alg a, const void* data, size_t len) {
    return ToHex(Hash(a, data, len));
}

std::string HashFileHex(Alg a, const std::wstring& path) {
    return ToHex(HashFile(a, path));
}

bool Equals(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}

} // namespace xgcrypto
