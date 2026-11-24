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
#include <memory>
#include <string>
#include <vector>

// XGZIP —— zip 读取、解压与写入（Windows / C++17 / 无第三方依赖）
//
// 解压：列目录、读单个条目、整包解到目录。deflate 解压是库内自带的原始实现
// （store / fixed / dynamic 三种块），不链 zlib。
// 写入：Store（原样打包）或 Deflate（自带固定 Huffman 压缩器），支持从磁盘文件、
// 内存缓冲和目录条目组装新包；先写 `<zip>.tmp`，成功后一步改名，失败不留半成品。
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

// ---------------- 写入 ----------------

enum class WriteMethod {
    Store,      // 不压缩，流式复制（内存占用恒定，适合大文件）
    Deflate,    // 固定 Huffman 压缩；条目会先在内存里压完再写，单条大小受内存限制
};

struct WriteOptions {
    WriteMethod method = WriteMethod::Store;
    int effort = 6;             // Deflate 的搜索强度 1..9
    bool preserveMtime = true;  // 从磁盘文件取修改时间写进条目的 DOS 时间
    bool utf8Names = true;      // 条目名按 UTF-8 存（写通用标志位 0x0800）
};

// 顺序写一个新 zip。典型用法：
//   Writer w(path); w.Begin(err); w.AddFile(L"a\\b.txt", disk, err); w.Commit(err);
class Writer {
public:
    explicit Writer(const std::wstring& zipPath, const WriteOptions& opt = WriteOptions{});
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    bool Begin(std::string& err);
    // archiveName 里的 '/' 和 '\\' 都会存成 '/'，做基本规范化（拒绝 .. 与盘符）
    bool AddFile(const std::wstring& archiveName, const std::wstring& diskPath, std::string& err);
    bool AddMemory(const std::wstring& archiveName, const void* data, size_t len, std::string& err);
    bool AddDirectory(const std::wstring& archiveName, std::string& err);

    // 写中央目录并改名到最终路径
    bool Commit(std::string& err);
    // 放弃：删掉临时文件
    void Abort();

    uint64_t BytesWritten() const;
    uint32_t EntriesWritten() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xgzip
