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

#include "xgcrash/xgcrash.h"

#include <windows.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <mutex>

#pragma comment(lib, "dbghelp.lib")

namespace xgcrash {

namespace {

constexpr DWORD kFatalCodes[] = {
    EXCEPTION_ACCESS_VIOLATION,
    EXCEPTION_ARRAY_BOUNDS_EXCEEDED,
    EXCEPTION_DATATYPE_MISALIGNMENT,
    EXCEPTION_FLT_DIVIDE_BY_ZERO,
    EXCEPTION_ILLEGAL_INSTRUCTION,
    EXCEPTION_INT_DIVIDE_BY_ZERO,
    EXCEPTION_STACK_OVERFLOW,
};

struct State {
    std::mutex mu;
    Options opt;
    bool installed = false;
    bool fullMemory = false;
    LPTOP_LEVEL_EXCEPTION_FILTER prevFilter = nullptr;
    std::terminate_handler prevTerminate = nullptr;
    void* veh = nullptr;
    std::atomic<bool> dumping{ false };
};

State& S() {
    static State s;
    return s;
}

std::wstring AppNameFromExe() {
    wchar_t path[MAX_PATH * 2] = {};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH * 2)) return L"app";
    const wchar_t* name = wcsrchr(path, L'\\');
    std::wstring out = name ? name + 1 : path;
    size_t dot = out.find_last_of(L'.');
    if (dot != std::wstring::npos) out.resize(dot);
    return out.empty() ? L"app" : out;
}

std::wstring Stamp() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t buf[64];
    swprintf_s(buf, L"%04u%02u%02u_%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
               t.wSecond);
    return buf;
}

bool EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return false;
    std::wstring cur;
    for (size_t i = 0; i < dir.size(); ++i) {
        cur.push_back(dir[i]);
        bool boundary = dir[i] == L'\\' || dir[i] == L'/' || i + 1 == dir.size();
        if (!boundary) continue;
        if (cur.size() <= 3 && cur[1] == L':') continue;
        CreateDirectoryW(cur.c_str(), nullptr);
    }
    return GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring MakeDumpPath(const std::wstring& tag) {
    State& s = S();
    std::wstring dir = s.opt.dumpDir;
    const std::wstring app = s.opt.appName.empty() ? L"app" : s.opt.appName;
    std::wstring name = app + L"_" + Stamp();
    wchar_t tail[64];
    swprintf_s(tail, L"_%lu_%ls", (unsigned long)GetCurrentProcessId(),
               tag.empty() ? L"crash" : tag.c_str());
    name += tail;
    for (int i = 0; i < 100; ++i) {
        std::wstring candidate = dir + L"\\" + name + (i ? L"_" + std::to_wstring(i) : L"") + L".dmp";
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) return candidate;
    }
    return dir + L"\\" + name + L".dmp";
}

std::wstring DoWriteDump(void* exceptionPointers, const std::wstring& tag) {
    State& s = S();
    if (s.dumping.exchange(true)) return {};   // 已经有人在写，别再套娃

    std::wstring dir = s.opt.dumpDir.empty() ? DefaultDumpDir() : s.opt.dumpDir;
    EnsureDir(dir);
    s.opt.dumpDir = dir;

    std::wstring path = MakeDumpPath(tag);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        s.dumping.store(false);
        return {};
    }

    MINIDUMP_TYPE type = s.opt.fullMemory ? MiniDumpWithFullMemory : MiniDumpNormal;
    if (!s.opt.fullMemory) type = (MINIDUMP_TYPE)(type | MiniDumpWithHandleData |
                                                  MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    MINIDUMP_EXCEPTION_INFORMATION info{};
    MINIDUMP_EXCEPTION_INFORMATION* pinfo = nullptr;
    if (exceptionPointers) {
        info.ThreadId = GetCurrentThreadId();
        info.ExceptionPointers = (PEXCEPTION_POINTERS)exceptionPointers;
        info.ClientPointers = FALSE;
        pinfo = &info;
    }
    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, pinfo,
                                nullptr, nullptr);
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());

    s.dumping.store(false);
    if (!ok) return {};

    if (s.opt.onWritten) {
        auto cb = s.opt.onWritten;
        cb(path);
    }
    PruneDumps(s.opt.maxDumps);
    return path;
}

LONG WINAPI FilterProc(EXCEPTION_POINTERS* ep) {
    State& s = S();
    DoWriteDump(ep, L"crash");
    if (s.prevFilter) s.prevFilter(ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

LONG WINAPI VehProc(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    bool fatal = false;
    for (DWORD c : kFatalCodes) if (c == code) { fatal = true; break; }
    // 栈已经没了就别再进 dump 代码（会在里面二次崩）
    if (fatal && code != EXCEPTION_STACK_OVERFLOW) DoWriteDump(ep, L"veh");
    return EXCEPTION_CONTINUE_SEARCH;
}

void TerminateProc() {
    DoWriteDump(nullptr, L"terminate");
    TerminateProcess(GetCurrentProcess(), 3);
}

} // namespace

std::wstring DefaultDumpDir() {
    wchar_t* local = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local) {
        base = local;
        CoTaskMemFree(local);
    } else {
        wchar_t tmp[MAX_PATH * 2] = {};
        GetTempPathW(MAX_PATH * 2, tmp);
        base = tmp;
    }
    std::wstring app = AppNameFromExe();
    return base + L"\\" + app + L"\\CrashDumps";
}

bool Install(const Options& opt) {
    Shutdown();
    State& s = S();
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.opt = opt;
        s.fullMemory = opt.fullMemory;
        if (s.opt.appName.empty()) s.opt.appName = AppNameFromExe();
        if (s.opt.dumpDir.empty()) s.opt.dumpDir = DefaultDumpDir();
        s.installed = true;
    }
    EnsureDir(s.opt.dumpDir);

    s.prevFilter = SetUnhandledExceptionFilter(FilterProc);
    if (opt.catchTerminate) {
        s.prevTerminate = std::get_terminate();
        std::set_terminate(TerminateProc);
    }
    if (opt.catchVectored) s.veh = AddVectoredExceptionHandler(1, VehProc);

    PruneDumps(s.opt.maxDumps);
    return true;
}

void Shutdown() {
    State& s = S();
    bool was = false;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        was = s.installed;
        s.installed = false;
    }
    if (!was) return;
    if (s.veh) {
        RemoveVectoredExceptionHandler(s.veh);
        s.veh = nullptr;
    }
    if (s.prevTerminate) {
        std::set_terminate(s.prevTerminate);
        s.prevTerminate = nullptr;
    }
    if (s.prevFilter) {
        SetUnhandledExceptionFilter(s.prevFilter);
        s.prevFilter = nullptr;
    }
}

bool WriteDump(void* exceptionPointers, const std::wstring& tag, std::wstring* outPath) {
    std::wstring p = DoWriteDump(exceptionPointers, tag);
    if (outPath) *outPath = p;
    return !p.empty();
}

std::vector<std::wstring> ListDumps() {
    State& s = S();
    std::wstring dir = s.opt.dumpDir.empty() ? DefaultDumpDir() : s.opt.dumpDir;
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*.dmp").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        out.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

size_t PruneDumps(uint32_t maxDumps) {
    if (!maxDumps) return 0;
    auto dumps = ListDumps();
    if (dumps.size() <= maxDumps) return 0;
    size_t removed = 0;
    for (size_t i = 0; i < dumps.size() - maxDumps; ++i)
        if (DeleteFileW(dumps[i].c_str())) ++removed;
    return removed;
}

} // namespace xgcrash
