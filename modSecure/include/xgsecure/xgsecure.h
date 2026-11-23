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

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// XGSECURE —— 编解码与密钥材料（Windows / C++17 / 静态库 / Apache-2.0）
//
// XGCRYPTO 只算摘要；这里补上要处理密钥的那部分，全部走 CNG (BCrypt)：
//   * Base64 / Base64Url（PKCE 的 verifier/challenge、Bearer 头、JWT 片段）
//   * 密码学随机（state / nonce / device code / salt）
//   * HMAC-SHA256（给自定义 API 签名、Webhook 校验）
//   * AES-256-GCM（token 落盘加密：authenticated，篡改会被发现）
//   * PBKDF2-HMAC-SHA256（口令 → 密钥）
//
// 刻意不做：证书/签名（用 CNG 原生接口或 libcurl 的 schannel）、协商/握手、
// 自研密码算法。这里只是把 CNG 的调用顺序固定下来，省得每个项目重写一遍还写错。

namespace xgsecure {

// ---------- Base64 ----------

std::string Base64Encode(const void* data, size_t len);
std::string Base64Encode(const std::vector<uint8_t>& data);

// URL 安全变体：'+/' 换成 '-_'，并且不带 '=' 填充（RFC 4648 §5 / JWT / PKCE）
std::string Base64UrlEncode(const void* data, size_t len);
std::string Base64UrlEncode(const std::vector<uint8_t>& data);

// 两种字母表都接受，忽略空白与 '='；遇到非法字符或长度不对返回 false
bool Base64Decode(const std::string& text, std::vector<uint8_t>& out);

// ---------- 随机 ----------

// BCryptGenRandom + 系统首选 RNG；失败返回空/ false
bool RandomBytes(void* out, size_t len);
std::vector<uint8_t> RandomBytes(size_t len);
std::string RandomHex(size_t bytes);            // 小写十六进制，长度 = bytes * 2
std::string RandomBase64Url(size_t bytes);      // 无填充，适合放 URL / 文件名

// ---------- HMAC ----------

std::vector<uint8_t> HmacSha256(const void* key, size_t keyLen, const void* data, size_t len);
std::vector<uint8_t> HmacSha256(const std::vector<uint8_t>& key, const void* data, size_t len);
std::string HmacSha256Hex(const void* key, size_t keyLen, const void* data, size_t len);
std::string HmacSha256Hex(const std::string& key, const std::string& data);

// 与 HmacSha256Hex 结果做定长比较（自己拼 == 会被提前返回的时序泄漏）
bool HmacSha256Verify(const std::string& key, const std::string& data, const std::string& hex);

// ---------- AES-256-GCM ----------

// 密封后的字节布局（平坦、自带版本号，便于以后换算法）：
//   [0]      version = 1
//   [1..12]  iv（随机 12 字节）
//   [13..28] tag（16 字节）
//   [29..]   ciphertext（长度与明文一致）
// 每次 Seal 都随机生成 iv——GCM 下重复 iv 会直接毁掉机密性，不要自备 iv。
bool Seal(const std::vector<uint8_t>& key32, const void* plain, size_t len, std::vector<uint8_t>& out,
          const void* aad = nullptr, size_t aadLen = 0);
bool Unseal(const std::vector<uint8_t>& key32, const void* sealed, size_t len, std::vector<uint8_t>& plain,
            const void* aad = nullptr, size_t aadLen = 0);

bool SealText(const std::vector<uint8_t>& key32, const std::string& plain, std::string& out,
              const std::string& aad = std::string());
bool UnsealText(const std::vector<uint8_t>& key32, const std::string& sealed, std::string& plain,
                const std::string& aad = std::string());

// ---------- 口令派生 ----------

// PBKDF2-HMAC-SHA256；默认 210000 次（OWASP 2023 对 PBKDF2-SHA256 的建议下限）
std::vector<uint8_t> KeyFromPassphrase(const std::string& passphrase, const std::vector<uint8_t>& salt,
                                       uint32_t iterations = 210000);

// 生成随机 32 字节 AES 密钥的便利函数
std::vector<uint8_t> NewAesKey();

} // namespace xgsecure
