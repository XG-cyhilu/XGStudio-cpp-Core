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

#include "xgproc/xgproc.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace xgproc {
namespace {

int64_t NowMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>((u.QuadPart - 116444736000000000ull) / 10000ull);
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// 严格校验 UTF-8（拒绝过长编码、代理区、超范围），避免把 GBK 字节误判成 UTF-8。
bool IsValidUtf8(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t need;
        unsigned int min;
        if (c < 0x80) { ++i; continue; }
        else if ((c & 0xE0) == 0xC0) { need = 1; min = 0x80; c &= 0x1F; }
        else if ((c & 0xF0) == 0xE0) { need = 2; min = 0x800; c &= 0x0F; }
        else if ((c & 0xF8) == 0xF0) { need = 3; min = 0x10000; c &= 0x07; }
        else return false;
        if (i + need >= n) return false;
        unsigned int v = c;
        for (size_t k = 1; k <= need; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            v = (v << 6) | (cc & 0x3F);
        }
        if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return false;
        i += need + 1;
    }
    return true;
}

std::string ToUtf8FromEnc(const std::string& s, Encoding e) {
    if (s.empty() || e == Encoding::Utf8) return s;
    int cp = CP_UTF8;
    if (e == Encoding::Auto) {
        if (IsValidUtf8(s)) return s;
        cp = GetOEMCP();
    } else if (e == Encoding::Oem) {
        cp = GetOEMCP();
    } else {
        cp = GetACP();
    }
    int n = MultiByteToWideChar(cp, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return s;
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(cp, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return ToUtf8(w);
}

} // namespace

std::wstring Quote(const std::wstring& arg) {
    bool need = arg.empty();
    for (wchar_t c : arg)
        if (c == L' ' || c == L'\t' || c == L'"') { need = true; break; }
    if (!need) return arg;

    std::wstring r;
    r.reserve(arg.size() + 2);
    r += L'"';
    size_t back = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++back;
        } else if (c == L'"') {
            r.append(back * 2 + 1, L'\\');
            r += L'"';
            back = 0;
        } else {
            if (back) { r.append(back, L'\\'); back = 0; }
            r += c;
        }
    }
    if (back) r.append(back * 2, L'\\');   // 结尾反斜杠要翻倍，否则会吃掉收尾引号
    r += L'"';
    return r;
}

std::wstring JoinArgs(const std::vector<std::wstring>& args) {
    std::wstring cmd;
    for (const auto& a : args) {
        if (!cmd.empty()) cmd += L' ';
        cmd += Quote(a);
    }
    return cmd;
}

struct Process::Impl {
    HANDLE proc = nullptr;
    HANDLE job = nullptr;
    HANDLE stdinW = nullptr;   // 父进程写
    HANDLE stdoutR = nullptr;  // 父进程读
    HANDLE stderrR = nullptr;

    std::thread tOut, tErr, tWait;

    std::mutex mu;
    std::condition_variable cv;
    std::deque<Line> queue;
    uint32_t cap = 4096;
    uint32_t lineBytes = 8192;
    Encoding enc = Encoding::Auto;
    bool eofOut = false;
    bool eofErr = false;
    std::atomic<uint64_t> dropped{ 0 };

    std::mutex exitMu;
    std::condition_variable exitCv;
    bool exited = false;
    int exitCode = INT32_MIN;
    std::atomic<bool> running{ false };
    DWORD pid = 0;

    void Push(Line&& ln) {
        std::lock_guard<std::mutex> lk(mu);
        if (queue.size() >= cap) {
            dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        queue.push_back(std::move(ln));
        cv.notify_one();
    }

    void Emit(std::string&& raw, bool isErr) {
        Line ln;
        ln.timeMs = NowMs();
        ln.stderrLine = isErr;
        ln.text = ToUtf8FromEnc(raw, enc);
        Push(std::move(ln));
    }

    void Reader(HANDLE pipe, bool isErr) {
        char buf[4096];
        std::string cur;
        cur.reserve(1024);
        while (true) {
            DWORD got = 0;
            if (!ReadFile(pipe, buf, sizeof buf, &got, nullptr) || got == 0) break;
            for (DWORD i = 0; i < got; ++i) {
                char c = buf[i];
                if (c == '\n') {
                    if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                    Emit(std::move(cur), isErr);
                    cur.clear();
                } else {
                    cur.push_back(c);
                    if (cur.size() >= lineBytes) {   // 单行过长：强制断开，别把内存吃满
                        Emit(std::move(cur), isErr);
                        cur.clear();
                    }
                }
            }
        }
        if (!cur.empty()) Emit(std::move(cur), isErr);
        std::lock_guard<std::mutex> lk(mu);
        (isErr ? eofErr : eofOut) = true;
        cv.notify_all();
    }

    void Waiter() {
        WaitForSingleObject(proc, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(proc, &code);
        {
            std::lock_guard<std::mutex> lk(exitMu);
            exitCode = static_cast<int>(code);
            exited = true;
        }
        running.store(false);
        exitCv.notify_all();
    }

    void CloseHandles() {
        for (HANDLE* h : { &stdinW, &stdoutR, &stderrR }) {
            if (*h) { CloseHandle(*h); *h = nullptr; }
        }
    }

    void KillNow() {
        if (job) TerminateJobObject(job, 1);
        else if (proc) TerminateProcess(proc, 1);
    }
};

Process::Process() : impl_(std::make_unique<Impl>()) {}

Process::~Process() {
    impl_->KillNow();
    if (impl_->tOut.joinable()) impl_->tOut.join();
    if (impl_->tErr.joinable()) impl_->tErr.join();
    if (impl_->tWait.joinable()) impl_->tWait.join();
    impl_->CloseHandles();
    if (impl_->proc) CloseHandle(impl_->proc);
    if (impl_->job) CloseHandle(impl_->job);
}

bool Process::Start(const std::wstring& commandLine, const std::wstring& workDir) {
    Options opt;
    opt.commandLine = commandLine;
    opt.workDir = workDir;
    return Start(opt);
}

bool Process::Start(const Options& opt) {
    if (impl_->proc) return false;      // 一个实例只起一次

    impl_->cap = opt.queueCapacity ? opt.queueCapacity : 1;
    impl_->lineBytes = opt.lineBytes ? opt.lineBytes : 512;
    impl_->enc = opt.outputEncoding;

    SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE inR = nullptr, outW = nullptr, errW = nullptr;
    if (!CreatePipe(&inR, &impl_->stdinW, &sa, 0)) return false;
    if (!CreatePipe(&impl_->stdoutR, &outW, &sa, 0)) return false;
    if (!CreatePipe(&impl_->stderrR, &errW, &sa, 0)) return false;

    // 父进程侧的三个句柄不被子进程继承，其余保留继承
    SetHandleInformation(impl_->stdinW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(impl_->stdoutR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(impl_->stderrR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError = errW;

    // 环境块：现有环境去掉被覆盖的键，再追加
    std::vector<wchar_t> envStorage;
    LPVOID envPtr = nullptr;
    if (!opt.env.empty()) {
        std::vector<std::wstring> entries;
        if (wchar_t* block = GetEnvironmentStringsW()) {
            for (const wchar_t* p = block; *p; p += wcslen(p) + 1) entries.emplace_back(p);
            FreeEnvironmentStringsW(block);
        }
        for (const auto& kv : opt.env) {
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const std::wstring& e) {
                size_t eq = e.find(L'=');
                if (eq == std::wstring::npos) return false;
                return _wcsicmp(e.substr(0, eq).c_str(), kv.first.c_str()) == 0;
            }), entries.end());
            entries.push_back(kv.first + L"=" + kv.second);
        }
        for (const auto& e : entries) {
            envStorage.insert(envStorage.end(), e.begin(), e.end());
            envStorage.push_back(0);
        }
        envStorage.push_back(0);
        envPtr = envStorage.data();
    }

    PROCESS_INFORMATION pi{};
    std::wstring cmd = opt.commandLine;
    std::wstring cwd = opt.workDir;

    BOOL ok = CreateProcessW(nullptr,
                             cmd.empty() ? nullptr : cmd.data(),
                             nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             envPtr,
                             cwd.empty() ? nullptr : cwd.c_str(),
                             &si, &pi);
    CloseHandle(inR);
    CloseHandle(outW);
    CloseHandle(errW);

    if (!ok) {
        impl_->CloseHandles();
        return false;
    }

    CloseHandle(pi.hThread);
    impl_->proc = pi.hProcess;
    impl_->pid = pi.dwProcessId;
    impl_->running.store(true);

    // Job 对象：Stop 时连同子孙一起收掉，也避免启动器退出后留下孤儿
    impl_->job = CreateJobObjectW(nullptr, nullptr);
    if (impl_->job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation, &info, sizeof info);
        AssignProcessToJobObject(impl_->job, impl_->proc);   // 失败不致命，退化成直接 TerminateProcess
    }

    Impl* p = impl_.get();
    impl_->tOut = std::thread([p] { p->Reader(p->stdoutR, false); });
    impl_->tErr = std::thread([p] { p->Reader(p->stderrR, true); });
    impl_->tWait = std::thread([p] { p->Waiter(); });
    return true;
}

bool Process::Poll(Line& out, uint32_t timeoutMs) {
    std::unique_lock<std::mutex> lk(impl_->mu);
    if (impl_->queue.empty() && timeoutMs) {
        impl_->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [this] {
            return !impl_->queue.empty() || (impl_->eofOut && impl_->eofErr);
        });
    }
    if (impl_->queue.empty()) return false;
    out = std::move(impl_->queue.front());
    impl_->queue.pop_front();
    return true;
}

bool Process::WriteStdin(const std::string& bytes) {
    if (!impl_->stdinW || bytes.empty()) return false;
    DWORD written = 0;
    return WriteFile(impl_->stdinW, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE;
}

bool Process::WriteStdin(const std::wstring& text) {
    return WriteStdin(ToUtf8(text));
}

bool Process::IsRunning() const {
    return impl_->running.load();
}

bool Process::Wait(uint32_t timeoutMs) {
    std::unique_lock<std::mutex> lk(impl_->exitMu);
    if (impl_->exited) return true;
    return impl_->exitCv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                                  [this] { return impl_->exited; });
}

int Process::ExitCode() const {
    std::lock_guard<std::mutex> lk(impl_->exitMu);
    return impl_->exited ? impl_->exitCode : INT32_MIN;
}

uint32_t Process::Pid() const {
    return static_cast<uint32_t>(impl_->pid);
}

uint64_t Process::Dropped() const {
    return impl_->dropped.load(std::memory_order_relaxed);
}

void Process::Stop(uint32_t graceMs) {
    if (!impl_->proc) return;
    if (impl_->running.load()) {
        if (graceMs && Wait(graceMs)) return;
        impl_->KillNow();
        Wait(1000);
    }
    if (impl_->tOut.joinable()) impl_->tOut.join();
    if (impl_->tErr.joinable()) impl_->tErr.join();
    if (impl_->tWait.joinable()) impl_->tWait.join();
    impl_->CloseHandles();
}

Result Process::Drain(uint32_t timeoutMs) {
    Result r;
    r.started = impl_->proc != nullptr;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        Line ln;
        while (Poll(ln, 0)) r.out.push_back(std::move(ln));

        bool done;
        {
            std::lock_guard<std::mutex> lk(impl_->mu);
            done = !impl_->running.load() && impl_->queue.empty() && impl_->eofOut && impl_->eofErr;
        }
        if (done) break;
        if (timeoutMs && std::chrono::steady_clock::now() > deadline) {
            r.timedOut = true;
            Stop(0);
            while (Poll(ln, 0)) r.out.push_back(std::move(ln));
            break;
        }
        if (Poll(ln, 20)) r.out.push_back(std::move(ln));
    }
    Wait(500);
    r.exitCode = ExitCode();
    r.dropped = Dropped();
    return r;
}

Result Run(const std::wstring& commandLine, uint32_t timeoutMs, const std::wstring& workDir) {
    Process p;
    Options opt;
    opt.commandLine = commandLine;
    opt.workDir = workDir;
    if (!p.Start(opt)) return Result{};
    return p.Drain(timeoutMs);
}

} // namespace xgproc
