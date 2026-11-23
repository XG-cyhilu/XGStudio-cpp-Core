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

// XGFS —— 文件与路径工具（Windows / C++17 / 静态库 / Apache-2.0）
//
// 只收那些"每个项目都会重新写一遍、每次都可能写错"的部分：
// 路径规范化、长路径、原子写、临时文件、递归遍历、大小统计、目录替换、安全删除。
// 不提供监控（ReadDirectoryChangesW 另算）、不提供 mmap、不提供压缩——那些各有取舍。

namespace xgfs {

// ---------- 路径 ----------

// 展开成绝对路径并统一成 '\\'，去掉多余分隔符与末尾 '\\'（根目录除外）
// 注意：Normalize 本身不做长路径改造，超长路径请过一遍 LongPath() 再用
std::wstring Normalize(const std::wstring& path);
std::wstring Join(const std::wstring& a, const std::wstring& b);

// 超过 MAX_PATH 时加 `\\?\`（UNC 转 `\\?\UNC\`）；短路径原样返回绝对化结果。
// 本库内部的 CreateFile / Find / Move / Delete 已经自动套用，所以正常调用不用手动处理；
// 只有把路径交给第三方库（libcurl、自定义 DLL）时才需要显式调用。
std::wstring LongPath(const std::wstring& path);

std::wstring FileName(const std::wstring& path);      // a\b\c.txt -> c.txt
std::wstring Stem(const std::wstring& path);          // a\b\c.txt -> c
std::wstring Extension(const std::wstring& path);     // a\b\c.txt -> .txt（含点，没有则为空）
std::wstring Parent(const std::wstring& path);        // a\b\c.txt -> a\b
std::wstring ReplaceExtension(const std::wstring& path, const std::wstring& ext);   // ext 可带点也可不带

// ---------- 查询 ----------

bool Exists(const std::wstring& path);
bool IsDir(const std::wstring& path);
bool IsFile(const std::wstring& path);
bool Size(const std::wstring& path, uint64_t& bytes);      // 目录返回 false
bool ModifiedTime(const std::wstring& path, int64_t& ms);  // 本地时区，毫秒
bool SetModifiedTime(const std::wstring& path, int64_t ms);
bool EnsureDir(const std::wstring& path, std::string* err = nullptr);

// 目录（含子目录）里所有普通文件的大小之和；不跟随 reparse point
bool DirSize(const std::wstring& dir, uint64_t& bytes, std::string* err = nullptr);

// 所在卷的可用/总容量（字节）。path 必须存在（文件或目录都行）。
// 下载前判断"空间够不够"用它，别自己拼 GetDiskFreeSpaceEx。
bool DiskSpace(const std::wstring& path, uint64_t& freeBytes, uint64_t& totalBytes, std::string* err = nullptr);

// ---------- 读写 ----------

bool ReadFile(const std::wstring& path, std::string& out, std::string* err = nullptr);   // 二进制安全
bool ReadFile(const std::wstring& path, std::vector<uint8_t>& out, std::string* err = nullptr);
bool WriteFile(const std::wstring& path, const void* data, size_t len, std::string* err = nullptr);
bool WriteFile(const std::wstring& path, const std::string& text, std::string* err = nullptr);

// 同目录临时文件写完 + FlushFileBuffers + MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)。
// 断电/崩溃时要么是旧文件，要么是新文件，不会留下半截配置。
bool WriteFileAtomic(const std::wstring& path, const void* data, size_t len, std::string* err = nullptr);
bool WriteFileAtomic(const std::wstring& path, const std::string& text, std::string* err = nullptr);

// ---------- 移动/复制/删除 ----------

bool Move(const std::wstring& src, const std::wstring& dst, bool overwrite, std::string* err = nullptr);
bool Copy(const std::wstring& src, const std::wstring& dst, bool overwrite, std::string* err = nullptr);

// 递归删除：失败时清掉只读属性再重试（被杀软/索引器/刚写完的文件短暂占用很常见）
bool Remove(const std::wstring& path, bool recursive = false, std::string* err = nullptr);
bool RemoveWithRetry(const std::wstring& path, bool recursive = false, int retries = 5, int delayMs = 120,
                     std::string* err = nullptr);

// 原子式目录替换：把 src 整个换成 dst。
// 流程 = dst→dst.old（若存在）→ src→dst → 删 dst.old；第二步失败会把 dst.old 移回去。
// 解压/更新场景用它，避免"删了一半新目录没进来"。src 与 dst 必须同卷（rename 语义）。
bool ReplaceDirectory(const std::wstring& src, const std::wstring& dst, std::string* err = nullptr);

// ---------- 临时文件 ----------

// dirHint 为空时用系统 temp；生成唯一文件名并立即创建空文件
bool CreateTempFile(const std::wstring& dirHint, std::wstring& path, std::string* err = nullptr);
// 同上，但建的是空目录
bool CreateTempDir(const std::wstring& dirHint, std::wstring& path, std::string* err = nullptr);

// ---------- 遍历 ----------

struct DirEntry {
    std::wstring name;
    std::wstring path;      // 绝对路径
    uint64_t size = 0;
    int64_t mtimeMs = 0;
    bool isDir = false;
};

struct ListOptions {
    bool recursive = false;
    const wchar_t* pattern = nullptr;   // 例如 L"*.json"；空 = 全收
    bool files = true;
    bool dirs = true;
};

bool ListDir(const std::wstring& dir, std::vector<DirEntry>& out, const ListOptions& opt = {},
             std::string* err = nullptr);

} // namespace xgfs
