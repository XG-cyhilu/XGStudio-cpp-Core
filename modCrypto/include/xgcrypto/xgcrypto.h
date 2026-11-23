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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// XGCRYPTO —— 基于 CNG (BCrypt) 的摘要计算
//
// 只做"算哈希"这一件事：字节流和文件、一次性或分块、结果要字节还是十六进制。
// 不碰加密、签名、证书——那些需要密钥管理的场景不该由这个库假装能覆盖。

namespace xgcrypto {

enum class Alg {
    Md5,
    Sha1,
    Sha256,
};

// 算法的摘要长度（字节）：16 / 20 / 32
uint32_t DigestSize(Alg a);
const char* AlgName(Alg a);

// 分块喂数据。Final 之后不可再用（要重算就重新构造）。
class Digest {
public:
    explicit Digest(Alg a);
    ~Digest();
    Digest(const Digest&) = delete;
    Digest& operator=(const Digest&) = delete;

    bool Update(const void* data, size_t len);
    bool Update(const std::vector<uint8_t>& data) { return Update(data.data(), data.size()); }

    // 取结果；已 Final 则原样返回缓存，不会二次收尾。
    bool Final(std::vector<uint8_t>& out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 一次性
std::vector<uint8_t> Hash(Alg a, const void* data, size_t len);
std::vector<uint8_t> HashFile(Alg a, const std::wstring& path);

// 十六进制（小写，无分隔）
std::string ToHex(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> FromHex(const std::string& hex);   // 非法字符或奇数长度返回空

std::string HashHex(Alg a, const void* data, size_t len);
std::string HashFileHex(Alg a, const std::wstring& path);

// 定长比较，避免按前缀提前退出（校验哈希值时用这个，不要用 ==）
bool Equals(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);

} // namespace xgcrypto
