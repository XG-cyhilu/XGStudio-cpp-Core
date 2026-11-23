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

#include "xgsecure/xgsecure.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace xgsecure {
namespace {

constexpr uint8_t kSealVersion = 1;
constexpr size_t kIvLen = 12;
constexpr size_t kTagLen = 16;
constexpr size_t kHeaderLen = 1 + kIvLen + kTagLen;   // version + iv + tag

bool NtOk(NTSTATUS st) { return st >= 0; }

std::string HexOf(const uint8_t* p, size_t n) {
    static const char* kHex = "0123456789abcdef";
    std::string s;
    s.resize(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s[i * 2] = kHex[p[i] >> 4];
        s[i * 2 + 1] = kHex[p[i] & 0x0F];
    }
    return s;
}

// 打开一个算法句柄；失败返回 nullptr。hmac=true 时用 HMAC 标志并直接用 key 建哈希。
struct AlgHandle {
    BCRYPT_ALG_HANDLE h = nullptr;
    ~AlgHandle() {
        if (h) BCryptCloseAlgorithmProvider(h, 0);
    }
};

bool OpenSha256(AlgHandle& alg, bool hmac) {
    return NtOk(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_SHA256_ALGORITHM, nullptr,
                                            hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0));
}

// HMAC-SHA256 的核心：BCryptCreateHash 带 key，Update 一次，Finish 取 32 字节
bool HmacSha256Raw(const void* key, size_t keyLen, const void* data, size_t len, std::vector<uint8_t>& out) {
    AlgHandle alg;
    if (!OpenSha256(alg, true)) return false;

    BCRYPT_HASH_HANDLE hash = nullptr;
    NTSTATUS st = BCryptCreateHash(alg.h, &hash, nullptr, 0, const_cast<PUCHAR>(static_cast<const UCHAR*>(key)),
                                   static_cast<ULONG>(keyLen), 0);
    if (!NtOk(st)) return false;

    bool ok = NtOk(BCryptHashData(hash, const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),
                                  static_cast<ULONG>(len), 0));
    if (ok) {
        out.resize(32);
        ok = NtOk(BCryptFinishHash(hash, out.data(), 32, 0));
    }
    BCryptDestroyHash(hash);
    return ok;
}

} // namespace

// ---------------- Base64 ----------------

std::string Base64Encode(const void* data, size_t len) {
    static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t* in = static_cast<const uint8_t*>(data);
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        uint32_t v = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8) | in[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    if (i < len) {
        uint32_t v = uint32_t(in[i]) << 16;
        bool two = (i + 1 < len);
        if (two) v |= uint32_t(in[i + 1]) << 8;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += two ? kAlphabet[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string Base64Encode(const std::vector<uint8_t>& data) {
    return Base64Encode(data.data(), data.size());
}

std::string Base64UrlEncode(const void* data, size_t len) {
    std::string s = Base64Encode(data, len);
    for (auto& c : s) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!s.empty() && s.back() == '=') s.pop_back();
    return s;
}

std::string Base64UrlEncode(const std::vector<uint8_t>& data) {
    return Base64UrlEncode(data.data(), data.size());
}

bool Base64Decode(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    int8_t map[256];
    memset(map, -1, sizeof map);
    const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; ++i) map[static_cast<uint8_t>(a[i])] = static_cast<int8_t>(i);
    map[static_cast<uint8_t>('-')] = 62;      // base64url
    map[static_cast<uint8_t>('_')] = 63;

    std::vector<uint8_t> vals;
    vals.reserve(text.size());
    for (char c : text) {
        if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        int8_t v = map[static_cast<uint8_t>(c)];
        if (v < 0) return false;
        vals.push_back(static_cast<uint8_t>(v));
    }
    if (vals.size() % 4 == 1) return false;      // 1 个字符不可能解出任何字节

    out.reserve(vals.size() / 4 * 3 + 2);
    size_t i = 0;
    while (i + 3 < vals.size()) {
        uint32_t v = (uint32_t(vals[i]) << 18) | (uint32_t(vals[i + 1]) << 12) | (uint32_t(vals[i + 2]) << 6) |
                     vals[i + 3];
        out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        i += 4;
    }
    size_t rem = vals.size() - i;
    if (rem == 2) {
        uint32_t v = (uint32_t(vals[i]) << 18) | (uint32_t(vals[i + 1]) << 12);
        out.push_back(static_cast<uint8_t>(v >> 16));
    } else if (rem == 3) {
        uint32_t v = (uint32_t(vals[i]) << 18) | (uint32_t(vals[i + 1]) << 12) | (uint32_t(vals[i + 2]) << 6);
        out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    return true;
}

// ---------------- 随机 --------------

bool RandomBytes(void* out, size_t len) {
    if (!len) return true;
    return NtOk(BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), static_cast<ULONG>(len),
                               BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

std::vector<uint8_t> RandomBytes(size_t len) {
    std::vector<uint8_t> v(len);
    if (!RandomBytes(v.data(), v.size())) v.clear();
    return v;
}

std::string RandomHex(size_t bytes) {
    std::vector<uint8_t> v = RandomBytes(bytes);
    if (v.empty()) return {};
    return HexOf(v.data(), v.size());
}

std::string RandomBase64Url(size_t bytes) {
    std::vector<uint8_t> v = RandomBytes(bytes);
    if (v.empty()) return {};
    return Base64UrlEncode(v.data(), v.size());
}

// ---------------- HMAC --------------

std::vector<uint8_t> HmacSha256(const void* key, size_t keyLen, const void* data, size_t len) {
    std::vector<uint8_t> out;
    HmacSha256Raw(key, keyLen, data, len, out);
    return out;
}

std::vector<uint8_t> HmacSha256(const std::vector<uint8_t>& key, const void* data, size_t len) {
    return HmacSha256(key.data(), key.size(), data, len);
}

std::string HmacSha256Hex(const void* key, size_t keyLen, const void* data, size_t len) {
    std::vector<uint8_t> mac = HmacSha256(key, keyLen, data, len);
    if (mac.empty()) return {};
    return HexOf(mac.data(), mac.size());
}

std::string HmacSha256Hex(const std::string& key, const std::string& data) {
    return HmacSha256Hex(key.data(), key.size(), data.data(), data.size());
}

bool HmacSha256Verify(const std::string& key, const std::string& data, const std::string& hex) {
    std::string expect = HmacSha256Hex(key, data);
    if (expect.empty() || expect.size() != hex.size()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < expect.size(); ++i) diff |= static_cast<uint8_t>(expect[i] ^ hex[i]);
    return diff == 0;
}

// ---------------- AES-256-GCM --------------

bool Seal(const std::vector<uint8_t>& key32, const void* plain, size_t len, std::vector<uint8_t>& out,
          const void* aad, size_t aadLen) {
    out.clear();
    if (key32.size() != 32) return false;

    AlgHandle alg;
    if (!NtOk(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_AES_ALGORITHM, nullptr, 0))) return false;
    if (!NtOk(BCryptSetProperty(alg.h, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                               static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0)))
        return false;

    // key object 大小固定查出来，别假设
    ULONG objLen = 0, cb = 0;
    if (!NtOk(BCryptGetProperty(alg.h, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof objLen, &cb, 0)))
        return false;
    std::vector<uint8_t> keyObj(objLen);

    BCRYPT_KEY_HANDLE key = nullptr;
    if (!NtOk(BCryptGenerateSymmetricKey(alg.h, &key, keyObj.data(), objLen,
                                        const_cast<PUCHAR>(key32.data()), static_cast<ULONG>(key32.size()), 0)))
        return false;

    out.resize(kHeaderLen + len);
    out[0] = kSealVersion;
    uint8_t* iv = out.data() + 1;
    uint8_t* tag = out.data() + 1 + kIvLen;
    if (!RandomBytes(iv, kIvLen)) {
        BCryptDestroyKey(key);
        out.clear();
        return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = iv;
    info.cbNonce = static_cast<ULONG>(kIvLen);
    info.pbTag = tag;
    info.cbTag = static_cast<ULONG>(kTagLen);
    info.pbAuthData = const_cast<PUCHAR>(static_cast<const UCHAR*>(aad));
    info.cbAuthData = static_cast<ULONG>(aadLen);

    ULONG done = 0;
    PUCHAR cipher = out.data() + kHeaderLen;
    NTSTATUS st = BCryptEncrypt(key, const_cast<PUCHAR>(static_cast<const UCHAR*>(plain)), static_cast<ULONG>(len), &info,
                                nullptr, 0, cipher, static_cast<ULONG>(len), &done, 0);
    BCryptDestroyKey(key);
    if (!NtOk(st) || done != len) {
        out.clear();
        return false;
    }
    return true;
}

bool Unseal(const std::vector<uint8_t>& key32, const void* sealed, size_t len, std::vector<uint8_t>& plain,
            const void* aad, size_t aadLen) {
    plain.clear();
    if (key32.size() != 32 || len < kHeaderLen) return false;
    const uint8_t* p = static_cast<const uint8_t*>(sealed);
    if (p[0] != kSealVersion) return false;

    uint8_t* iv = const_cast<uint8_t*>(p + 1);
    uint8_t* tag = const_cast<uint8_t*>(p + 1 + kIvLen);
    const uint8_t* cipher = p + kHeaderLen;
    size_t clen = len - kHeaderLen;

    AlgHandle alg;
    if (!NtOk(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_AES_ALGORITHM, nullptr, 0))) return false;
    if (!NtOk(BCryptSetProperty(alg.h, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                               static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0)))
        return false;

    ULONG objLen = 0, cb = 0;
    if (!NtOk(BCryptGetProperty(alg.h, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof objLen, &cb, 0)))
        return false;
    std::vector<uint8_t> keyObj(objLen);

    BCRYPT_KEY_HANDLE key = nullptr;
    if (!NtOk(BCryptGenerateSymmetricKey(alg.h, &key, keyObj.data(), objLen,
                                        const_cast<PUCHAR>(key32.data()), static_cast<ULONG>(key32.size()), 0)))
        return false;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = iv;
    info.cbNonce = static_cast<ULONG>(kIvLen);
    info.pbTag = tag;
    info.cbTag = static_cast<ULONG>(kTagLen);
    info.pbAuthData = const_cast<PUCHAR>(static_cast<const UCHAR*>(aad));
    info.cbAuthData = static_cast<ULONG>(aadLen);

    std::vector<uint8_t> buf(clen ? clen : 1);
    ULONG done = 0;
    NTSTATUS st = BCryptDecrypt(key, const_cast<PUCHAR>(cipher), static_cast<ULONG>(clen), &info, nullptr, 0, buf.data(),
                                static_cast<ULONG>(clen), &done, 0);
    BCryptDestroyKey(key);
    if (!NtOk(st) || done != clen) return false;   // 认证失败（被改过 / key 不对）也走这里
    buf.resize(clen);
    plain.swap(buf);
    return true;
}

bool SealText(const std::vector<uint8_t>& key32, const std::string& plain, std::string& out, const std::string& aad) {
    std::vector<uint8_t> blob;
    if (!Seal(key32, plain.data(), plain.size(), blob, aad.data(), aad.size())) return false;
    // 落盘用 base64：配置文件里放二进制会被转义得很难看
    out = Base64Encode(blob);
    return true;
}

bool UnsealText(const std::vector<uint8_t>& key32, const std::string& sealed, std::string& plain, const std::string& aad) {
    std::vector<uint8_t> blob;
    if (!Base64Decode(sealed, blob)) return false;
    std::vector<uint8_t> data;
    if (!Unseal(key32, blob.data(), blob.size(), data, aad.data(), aad.size())) return false;
    plain.assign(data.begin(), data.end());
    return true;
}

// ---------------- 口令派生 --------------

std::vector<uint8_t> KeyFromPassphrase(const std::string& passphrase, const std::vector<uint8_t>& salt,
                                       uint32_t iterations) {
    AlgHandle alg;
    if (!OpenSha256(alg, true)) return {};
    std::vector<uint8_t> out(32);
    NTSTATUS st = BCryptDeriveKeyPBKDF2(
        alg.h, reinterpret_cast<PUCHAR>(const_cast<char*>(passphrase.data())),
        static_cast<ULONG>(passphrase.size()),
        const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()),
        static_cast<ULONGLONG>(iterations ? iterations : 1), out.data(), static_cast<ULONG>(out.size()), 0);
    if (!NtOk(st)) return {};
    return out;
}

std::vector<uint8_t> NewAesKey() { return RandomBytes(32); }

} // namespace xgsecure
