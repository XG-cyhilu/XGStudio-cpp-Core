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

#include <xgproc/xgproc.h>

#include <windows.h>

#include <cstdio>
#include <string>
#include <thread>

static int CountLines(const xgproc::Result& r, bool errOnly) {
    int n = 0;
    for (const auto& l : r.out)
        if (errOnly ? l.stderrLine : true) ++n;
    return n;
}

int main() {
    // 1) 一次性执行：stdout 捕获 + 退出码
    auto r1 = xgproc::Run(L"cmd /c echo hello && echo world && exit 3", 10'000);
    printf("[1] started=%d exit=%d lines=%d\n", r1.started, r1.exitCode, CountLines(r1, false));
    for (const auto& l : r1.out) printf("    > %s\n", l.text.c_str());

    // 2) stderr 分离
    auto r2 = xgproc::Run(L"cmd /c echo out-line && echo err-line 1>&2", 10'000);
    printf("[2] stdout=%d stderr=%d\n", CountLines(r2, false) - CountLines(r2, true), CountLines(r2, true));

    // 3) 环境变量覆盖
    xgproc::Options o3;
    o3.commandLine = L"cmd /c echo MARK=%XGPROC_MARK%";
    o3.env = { { L"XGPROC_MARK", L"injected-42" } };
    xgproc::Process p3;
    p3.Start(o3);
    auto r3 = p3.Drain(10'000);
    printf("[3] %s\n", r3.out.empty() ? "(no output)" : r3.out.front().text.c_str());

    // 4) 轮询逐行 + 中途 Stop 杀整棵树
    xgproc::Process p4;
    xgproc::Options o4;
    o4.commandLine = L"C:\\Windows\\System32\\ping.exe -n 100 127.0.0.1";
    p4.Start(o4);
    std::thread killer([&p4] {
        Sleep(1200);
        p4.Stop(200);
    });
    xgproc::Line ln;
    int got = 0;
    while (p4.Poll(ln, 250)) {
        if (got < 3) printf("[4] %s\n", ln.text.c_str());
        ++got;
    }
    killer.join();
    printf("[4] lines=%d running=%d exit=%d dropped=%llu\n", got, (int)p4.IsRunning(), p4.ExitCode(),
           static_cast<unsigned long long>(p4.Dropped()));
    p4.Stop(0);

    // 5) 引号转义自检
    printf("[5] %ls\n", xgproc::JoinArgs({ L"C:\\a b\\c.exe", L"--x=\"y z\"", L"tail\\" }).c_str());

    // 6) 超时：到点强制收掉并标记 timedOut
    auto r6 = xgproc::Run(L"C:\\Windows\\System32\\ping.exe -n 20 127.0.0.1", 1500);
    printf("[6] started=%d timedOut=%d exit=%d lines=%d\n", r6.started, r6.timedOut, r6.exitCode, CountLines(r6, false));
    for (size_t i = 0; i < r6.out.size() && i < 3; ++i) printf("    > %s\n", r6.out[i].text.c_str());
    return 0;
}
