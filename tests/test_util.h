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

// Tiny assertion + temp-dir helper shared by the ctest suites.
// No gtest: these tests must run on a bare windows-latest runner with zero downloads.

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace xgt {

inline int g_checks = 0;
inline int g_failures = 0;

inline void Report(bool ok, const char* expr, const char* file, int line, const char* extra) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    if (extra && extra[0])
        printf("FAIL %s:%d  %s  (%s)\n", file, line, expr, extra);
    else
        printf("FAIL %s:%d  %s\n", file, line, expr);
}

inline int Finish(const char* name) {
    printf("%s: %d checks, %d failed\n", name, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

// Fresh empty directory under %TEMP%, wiped before use.
inline std::wstring TempDir(const wchar_t* name) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"xgt_" + name + L"_" + std::to_wstring(GetCurrentProcessId());
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

inline void DropDir(const std::wstring& dir) {
    std::filesystem::remove_all(dir);
}

inline std::string ReadAll(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace xgt

#define XGT_CHECK(expr) xgt::Report(static_cast<bool>(expr), #expr, __FILE__, __LINE__, nullptr)
#define XGT_CHECK_MSG(expr, msg) xgt::Report(static_cast<bool>(expr), #expr, __FILE__, __LINE__, (msg))
#define XGT_FINISH(name) return xgt::Finish(name)
