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

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// XGZIP —— zip 读取与解压（Windows / C++17 / 无第三方依赖）
//
// 只做解：列目录、读单个条目、整包解到目录。
// deflate 解压是库内自带的原始实现（store / fixed / dynamic 三种块），不链 zlib。
//
// 安全口径：条目路径先做规范化，`..`、绝对路径、盘符一律拒绝，不会写出目标目录。

namespace xgzip {

struct Entry {
    std::wstring name;              // 相对路径，已把 '/' 换成 '\\'
    uint64_t size = 0;              // 解压后大小
    uint64_t compressedSize = 0;
    uint32_t crc32 = 0;
    uint16_t method = 0;            // 0 = store，8 = deflate
    bool isDir = false;
    bool encrypted = false;
};

struct Stats {
    std::atomic<uint64_t> bytesDone{ 0 };
    std::atomic<uint64_t> bytesTotal{ 0 };
    std::atomic<uint32_t> filesDone{ 0 };
    std::atomic<uint32_t> filesTotal{ 0 };

    void Reset() {
        bytesDone = 0;
        bytesTotal = 0;
        filesDone = 0;
        filesTotal = 0;
    }
};

struct ExtractOptions {
    std::wstring filter;        // 空 = 全部；支持 * ? 通配，或 `dir\` 前缀
    bool overwrite = true;      // false = 已存在则跳过
    bool keepPaths = true;      // false = 全部摊平到目标目录（重名会互相覆盖）
};

struct Result {
    bool ok = false;
    std::string error;                  // 第一个失败原因
    int files = 0;
    int dirs = 0;
    int skipped = 0;
    uint64_t bytes = 0;
    int64_t elapsedMs = 0;
    std::vector<std::wstring> failed;   // 失败的条目名
};

// 读中央目录。只读，不解压。
bool List(const std::wstring& zipPath, std::vector<Entry>& out, std::string& err);

// 解整包（或按 filter 解一部分）。stats 可为空，非空时由别的线程随时读取即可。
bool Extract(const std::wstring& zipPath, const std::wstring& destDir, const ExtractOptions& opt, Result& res,
             Stats* stats = nullptr);

// 把单个条目解到内存（配置、清单这类小文件用）。
bool ReadEntry(const std::wstring& zipPath, const std::wstring& name, std::vector<uint8_t>& out, std::string& err);

// 标准 CRC-32（zip 用的那个多项式），seed 传 0
uint32_t Crc32(uint32_t seed, const void* data, size_t len);

} // namespace xgzip
