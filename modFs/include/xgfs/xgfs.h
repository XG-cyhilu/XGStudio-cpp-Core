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
// 路径规范化、原子写、临时文件、递归遍历、安全删除。
// 不提供监控（ReadDirectoryChangesW 另算）、不提供 mmap、不提供压缩——那些各有取舍。

namespace xgfs {

// ---------- 路径 ----------

// 展开成绝对路径并统一成 '\\'，去掉多余分隔符与末尾 '\\'（根目录除外）
std::wstring Normalize(const std::wstring& path);
std::wstring Join(const std::wstring& a, const std::wstring& b);
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
bool Remove(const std::wstring& path, bool recursive = false, std::string* err = nullptr);

// ---------- 临时文件 ----------

// dirHint 为空时用系统 temp；生成唯一文件名并立即创建空文件
bool CreateTempFile(const std::wstring& dirHint, std::wstring& path, std::string* err = nullptr);

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
