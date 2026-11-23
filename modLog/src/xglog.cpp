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

#include "xglog/xglog.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace xglog {
namespace {

struct Item {
    Level level = Level::Info;
    int64_t timeMs = 0;
    uint32_t tid = 0;
    int line = 0;
    const char* file = nullptr;
    uint32_t slot = 0;
};

struct UiEntry {
    uint64_t seq = 0;
    int64_t timeMs = 0;
    Level level = Level::Info;
    uint32_t tid = 0;
    std::string text;
};

constexpr uint32_t kBatchMax = 256;

// 生命周期
std::mutex g_lifeMu;
std::atomic<bool> g_inited{ false };
Config g_cfg;
bool g_async = true;

// 队列（g_mu 保护）
std::mutex g_mu;
std::condition_variable g_cv;
std::vector<Item> g_ring;
std::vector<char> g_arena;
uint32_t g_head = 0;
uint32_t g_count = 0;
std::atomic<uint64_t> g_dropped{ 0 };

std::atomic<Level> g_level{ Level::Info };

std::thread g_writer;
std::atomic<bool> g_stop{ false };

// 输出端（只在写线程 / sync 模式互斥下使用）
std::mutex g_sinkMu;
std::FILE* g_file = nullptr;
uint64_t g_fileBytes = 0;
uint32_t g_rotateSeq = 0;
HANDLE g_console = nullptr;
bool g_consoleColor = false;
int64_t g_lastFlushMs = 0;

// UI 环（g_uiMu 保护）
std::vector<UiEntry> g_ui;
uint32_t g_uiPos = 0;
std::atomic<uint64_t> g_uiWriteSeq{ 0 };
std::atomic<uint64_t> g_uiReadSeq{ 0 };
std::mutex g_uiMu;

int64_t NowMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>((u.QuadPart - 116444736000000000ull) / 10000ull);
}

std::string TimeStamp(int64_t ms) {
    time_t secs = static_cast<time_t>(ms / 1000);
    struct tm lt {};
    localtime_s(&lt, &secs);
    char buf[40];
    sprintf_s(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
              lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
              lt.tm_hour, lt.tm_min, lt.tm_sec, static_cast<int>(ms % 1000));
    return std::string(buf);
}

const char* BaseName(const char* path) {
    if (!path) return "";
    const char* s = path;
    for (const char* p = path; *p; ++p)
        if (*p == '\\' || *p == '/') s = p + 1;
    return s;
}

void EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(dir), ec);
}

std::wstring ExeLogDir() {
    wchar_t path[MAX_PATH * 2] = { 0 };
    if (!GetModuleFileNameW(nullptr, path, ARRAYSIZE(path))) return L"logs";
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *slash = 0;
    return std::wstring(path) + L"\\logs";
}

std::wstring RotName(const std::wstring& base, uint32_t seq) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[96];
    swprintf_s(buf, L"%04d%02d%02d-%02d%02d%02d-%u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, seq);
    return base + L"-" + buf + L".log";
}

void PruneOld(const std::wstring& dir, const std::wstring& base, uint32_t keep) {
    namespace fs = std::filesystem;
    std::vector<fs::path> old;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::wstring name = it->path().filename().wstring();
        if (name.rfind(base + L"-", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, L".log") == 0)
            old.push_back(it->path());
    }
    if (old.size() <= keep) return;
    std::sort(old.begin(), old.end());
    for (size_t i = 0; i + keep < old.size(); ++i) {
        std::error_code rmEc;
        fs::remove(old[i], rmEc);
    }
}

void OpenLogFile() {
    if (!g_cfg.file) return;
    std::wstring dir = g_cfg.dir;
    EnsureDir(dir);
    std::wstring base = dir + L"\\" + g_cfg.baseName;
    if (std::filesystem::exists(base + L".log")) {
        std::error_code ec;
        std::filesystem::rename(base + L".log", RotName(base, ++g_rotateSeq), ec);
        if (ec) std::filesystem::remove(base + L".log", ec);
    }
    g_file = _wfopen((base + L".log").c_str(), L"wb");
    g_fileBytes = 0;
    if (g_file) setvbuf(g_file, nullptr, _IOFBF, 256 * 1024);
    PruneOld(dir, g_cfg.baseName, g_cfg.maxFiles);
}

void CloseLogFile() {
    if (g_file) {
        std::fflush(g_file);
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void RotateFile() {
    if (!g_file) return;
    CloseLogFile();
    std::wstring base = g_cfg.dir + L"\\" + g_cfg.baseName;
    std::error_code ec;
    std::filesystem::rename(base + L".log", RotName(base, ++g_rotateSeq), ec);
    g_file = _wfopen((base + L".log").c_str(), L"wb");
    g_fileBytes = 0;
    if (g_file) setvbuf(g_file, nullptr, _IOFBF, 256 * 1024);
    PruneOld(g_cfg.dir, g_cfg.baseName, g_cfg.maxFiles);
}

std::string FormatLine(Level lv, int64_t ts, uint32_t tid, const char* file, int line, const char* msg) {
    std::string s;
    s.reserve(256);
    s += '[';
    s += TimeStamp(ts);
    s += "][";
    s += LevelTag(lv);
    s += "][";
    char tidBuf[16];
    sprintf_s(tidBuf, sizeof tidBuf, "%u", tid);
    s += tidBuf;
    s += "] ";
    s += BaseName(file);
    char lnBuf[16];
    sprintf_s(lnBuf, sizeof lnBuf, ":%d ", line);
    s += lnBuf;
    s += msg;
    s += '\n';
    return s;
}

void PushUi(Level lv, int64_t ts, uint32_t tid, const char* file, int line, const char* msg) {
    if (g_ui.empty()) return;
    std::lock_guard<std::mutex> lk(g_uiMu);
    UiEntry& e = g_ui[g_uiPos];
    e.timeMs = ts;
    e.level = lv;
    e.tid = tid;
    e.text = BaseName(file);
    char lnBuf[16];
    sprintf_s(lnBuf, sizeof lnBuf, ":%d ", line);
    e.text += lnBuf;
    e.text += msg;
    e.seq = g_uiWriteSeq.load(std::memory_order_relaxed) + 1;
    g_uiWriteSeq.store(e.seq, std::memory_order_release);
    g_uiPos = (g_uiPos + 1) % static_cast<uint32_t>(g_ui.size());
}

void SinkLine(Level lv, int64_t ts, uint32_t tid, const char* file, int line, const char* msg) {
    PushUi(lv, ts, tid, file, line, msg);

    bool needFmt = false;
    if (g_console && static_cast<int>(lv) >= static_cast<int>(g_cfg.level)) needFmt = true;
    if (g_file && static_cast<int>(lv) >= static_cast<int>(g_cfg.fileLevel)) needFmt = true;
    if (!needFmt && !(g_cfg.debugger && IsDebuggerPresent() &&
                      static_cast<int>(lv) >= static_cast<int>(g_cfg.level)))
        return;

    std::string lineText = FormatLine(lv, ts, tid, file, line, msg);

    if (g_console && static_cast<int>(lv) >= static_cast<int>(g_cfg.level)) {
        std::string out;
        if (g_consoleColor) {
            const char* code = "\x1b[0m";
            switch (lv) {
            case Level::Trace: code = "\x1b[90m"; break;
            case Level::Debug: code = "\x1b[37m"; break;
            case Level::Info: code = "\x1b[36m"; break;
            case Level::Warn: code = "\x1b[33m"; break;
            case Level::Error: code = "\x1b[31m"; break;
            case Level::Fatal: code = "\x1b[1;41;97m"; break;
            default: break;
            }
            out = code + lineText + "\x1b[0m";
        } else {
            out = lineText;
        }
        DWORD n = 0;
        WriteFile(g_console, out.c_str(), static_cast<DWORD>(out.size()), &n, nullptr);
    }
    if (g_cfg.debugger && IsDebuggerPresent() &&
        static_cast<int>(lv) >= static_cast<int>(g_cfg.level))
        OutputDebugStringA(lineText.c_str());
    if (g_file && static_cast<int>(lv) >= static_cast<int>(g_cfg.fileLevel)) {
        std::fwrite(lineText.data(), 1, lineText.size(), g_file);
        g_fileBytes += lineText.size();
        if (g_cfg.maxFileBytes && g_fileBytes >= g_cfg.maxFileBytes) RotateFile();
    }
}

void FlushFileLocked() {
    if (g_file) std::fflush(g_file);
}

void WriterLoop() {
    std::vector<Item> batch;
    batch.reserve(kBatchMax);
    const uint32_t cap = static_cast<uint32_t>(g_ring.size());

    while (true) {
        bool stop = false;
        bool high = false;
        batch.clear();
        {
            std::unique_lock<std::mutex> lk(g_mu);
            if (g_count == 0 && !g_stop.load()) {
                g_cv.wait_for(lk, std::chrono::milliseconds(g_cfg.flushIntervalMs),
                              [] { return g_stop.load() || g_count > 0; });
            }
            stop = g_stop.load();
            uint32_t take = g_count < kBatchMax ? g_count : kBatchMax;
            for (uint32_t i = 0; i < take; ++i) {
                Item it = g_ring[g_head];
                g_head = (g_head + 1) % cap;
                if (static_cast<int>(it.level) >= static_cast<int>(Level::Warn)) high = true;
                batch.push_back(it);
            }
            g_count -= take;
        }
        if (!batch.empty()) {
            std::lock_guard<std::mutex> sl(g_sinkMu);
            for (const Item& it : batch)
                SinkLine(it.level, it.timeMs, it.tid, it.file, it.line,
                         &g_arena[static_cast<size_t>(it.slot) * g_cfg.messageMax]);

            int64_t now = NowMs();
            if (high || now - g_lastFlushMs >= static_cast<int64_t>(g_cfg.flushIntervalMs)) {
                FlushFileLocked();
                g_lastFlushMs = now;
            }
        }
        if (stop) {
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_count == 0) break;
        }
    }
    std::lock_guard<std::mutex> sl(g_sinkMu);
    FlushFileLocked();
}

void InitOnce(const Config& userCfg) {
    g_cfg = userCfg;
    if (g_cfg.dir.empty()) g_cfg.dir = ExeLogDir();
    if (g_cfg.queueCapacity == 0) g_cfg.queueCapacity = 1;
    if (g_cfg.uiCapacity == 0) g_cfg.uiCapacity = 1;
    if (g_cfg.messageMax < 32) g_cfg.messageMax = 32;
    g_async = g_cfg.async;

    g_ring.assign(g_cfg.queueCapacity, Item{});
    g_arena.assign(static_cast<size_t>(g_cfg.queueCapacity) * g_cfg.messageMax, 0);
    g_ui.assign(g_cfg.uiCapacity, UiEntry{});
    g_head = 0;
    g_count = 0;
    g_uiPos = 0;
    g_uiWriteSeq.store(0);
    g_uiReadSeq.store(0);
    g_dropped.store(0);
    g_level.store(g_cfg.level);
    g_lastFlushMs = NowMs();

    if (g_cfg.console) {
        g_console = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (g_console && g_console != INVALID_HANDLE_VALUE && GetConsoleMode(g_console, &mode)) {
            SetConsoleOutputCP(CP_UTF8);
            if (g_cfg.color && SetConsoleMode(g_console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
                g_consoleColor = true;
        } else {
            g_console = nullptr;
        }
    }
    OpenLogFile();

    g_stop.store(false);
    if (g_async) g_writer = std::thread(WriterLoop);
}

void EnsureInit() {
    if (g_inited.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lk(g_lifeMu);
    if (g_inited.load()) return;
    InitOnce(Config{});
    g_inited.store(true);
}

} // namespace

const wchar_t* LevelName(Level lv) {
    switch (lv) {
    case Level::Trace: return L"TRACE";
    case Level::Debug: return L"DEBUG";
    case Level::Info: return L"INFO";
    case Level::Warn: return L"WARN";
    case Level::Error: return L"ERROR";
    case Level::Fatal: return L"FATAL";
    default: return L"OFF";
    }
}

const char* LevelTag(Level lv) {
    switch (lv) {
    case Level::Trace: return "T";
    case Level::Debug: return "D";
    case Level::Info: return "I";
    case Level::Warn: return "W";
    case Level::Error: return "E";
    case Level::Fatal: return "F";
    default: return "-";
    }
}

bool Init(const Config& cfg) {
    std::lock_guard<std::mutex> lk(g_lifeMu);
    if (g_inited.load()) return true;
    InitOnce(cfg);
    g_inited.store(true);
    return true;
}

void Shutdown() {
    {
        std::lock_guard<std::mutex> lk(g_lifeMu);
        if (!g_inited.load()) return;
        g_stop.store(true);
        g_cv.notify_all();
        if (g_writer.joinable()) g_writer.join();
        {
            std::lock_guard<std::mutex> sl(g_sinkMu);
            CloseLogFile();
        }
        g_console = nullptr;
        g_consoleColor = false;
        g_inited.store(false);
    }
}

void Flush() {
    if (!g_inited.load()) return;
    if (g_async) {
        for (int i = 0; i < 60; ++i) {
            {
                std::lock_guard<std::mutex> lk(g_mu);
                if (g_count == 0) break;
            }
            g_cv.notify_one();
            Sleep(2);
        }
    }
    std::lock_guard<std::mutex> sl(g_sinkMu);
    FlushFileLocked();
}

void SetLevel(Level lv) {
    g_level.store(lv);
    g_cfg.level = lv;
}

Level GetLevel() {
    return g_level.load();
}

uint64_t Dropped() {
    return g_dropped.load(std::memory_order_relaxed);
}

bool UiPoll(Record& out) {
    if (!g_inited.load() || g_ui.empty()) return false;
    uint64_t read = g_uiReadSeq.load(std::memory_order_relaxed);
    uint64_t writeSeq = g_uiWriteSeq.load(std::memory_order_acquire);
    if (read >= writeSeq) return false;

    uint32_t cap = static_cast<uint32_t>(g_ui.size());
    if (writeSeq - read > cap) read = writeSeq - cap;

    std::lock_guard<std::mutex> lk(g_uiMu);
    uint32_t slot = static_cast<uint32_t>(read % cap);
    UiEntry& e = g_ui[slot];
    if (e.seq != read + 1) {          // 被覆盖，跳到最新避免 UI 追历史
        g_uiReadSeq.store(writeSeq);
        return false;
    }
    out.seq = read + 1;
    out.timeMs = e.timeMs;
    out.level = e.level;
    out.tid = e.tid;
    out.text = e.text;
    g_uiReadSeq.store(read + 1);
    return true;
}

void Logf(Level lv, const char* file, int line, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    VLogf(lv, file, line, fmt, ap);
    va_end(ap);
}

void VLogf(Level lv, const char* file, int line, const char* fmt, va_list ap) {
    if (static_cast<int>(lv) < static_cast<int>(g_level.load())) return;
    EnsureInit();

    char msg[1024];
    const uint32_t cap = g_cfg.messageMax;
    char* scratch = msg;
    std::vector<char> big;
    if (cap > sizeof msg) {
        big.resize(cap);
        scratch = big.data();
    }
    _vsnprintf_s(scratch, cap, _TRUNCATE, fmt, ap);

    if (!g_async) {
        std::lock_guard<std::mutex> sl(g_sinkMu);
        SinkLine(lv, NowMs(), GetCurrentThreadId(), file, line, scratch);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(g_mu);
        uint32_t ringCap = static_cast<uint32_t>(g_ring.size());
        if (g_count >= ringCap) {
            g_dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        uint32_t slot = (g_head + g_count) % ringCap;
        std::memcpy(&g_arena[static_cast<size_t>(slot) * cap], scratch, cap);
        Item& it = g_ring[slot];
        it.level = lv;
        it.timeMs = NowMs();
        it.tid = GetCurrentThreadId();
        it.file = file;
        it.line = line;
        it.slot = slot;
        ++g_count;
    }
    g_cv.notify_one();
}

} // namespace xglog
