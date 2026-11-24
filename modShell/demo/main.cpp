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

// xgshell_demo：逐项验证 xgshell 的能力，不弹窗、不改系统关联
//
// 会临时写一个快捷方式到 %TEMP%，并临时写/删 HKCU Run 下的 "XGShellDemo" 值。

#include <xgshell/xgshell.h>

#include <windows.h>
#include <cstdio>

int wmain() {
    // [1] 已知文件夹
    struct Item { xgshell::Folder id; const wchar_t* name; };
    const Item items[] = {
        {xgshell::Folder::LocalAppData, L"LocalAppData"},
        {xgshell::Folder::RoamingAppData, L"RoamingAppData"},
        {xgshell::Folder::Downloads, L"Downloads"},
        {xgshell::Folder::Temp, L"Temp"},
        {xgshell::Folder::Startup, L"Startup"},
    };
    int found = 0;
    for (auto& it : items) {
        std::wstring p = xgshell::KnownFolder(it.id);
        if (!p.empty()) ++found;
        std::wprintf(L"[1] %-16s %s\n", it.name, p.empty() ? L"<missing>" : p.c_str());
    }
    std::wprintf(L"[1] known-folders=%d/%d\n", found, (int)(sizeof(items) / sizeof(items[0])));

    // [2] 命令行解析与引号
    auto args = xgshell::ParseCommandLine(L"app.exe --path \"C:\\a b\\c\" plain");
    std::wprintf(L"[2] argc=%zu a2=%s a3=%s quote=%s\n", args.size(),
                 args.size() > 2 ? args[2].c_str() : L"", args.size() > 3 ? args[3].c_str() : L"",
                 xgshell::QuoteArg(L"C:\\a b\\c").c_str());

    // [3] 快捷方式往返
    std::wstring temp = xgshell::KnownFolder(xgshell::Folder::Temp);
    std::wstring lnk = xgshell::AppendPath(temp, L"xgshell_demo_test.lnk");
    xgshell::Shortcut sc;
    sc.linkPath = lnk;
    sc.target = L"C:\\Windows\\notepad.exe";
    sc.arguments = L"\"C:\\a b.txt\"";
    sc.workDir = L"C:\\Windows";
    sc.description = L"xgshell demo";
    sc.iconPath = L"C:\\Windows\\notepad.exe";
    bool created = xgshell::CreateShortcut(sc);
    xgshell::Shortcut back;
    bool read = created && xgshell::ReadShortcut(lnk, back);
    std::wprintf(L"[3] shortcut created=%d read=%d target=%s args=%s => %s\n",
                 (int)created, (int)read, back.target.c_str(), back.arguments.c_str(),
                 (read && back.target == sc.target) ? L"OK" : L"FAIL");
    DeleteFileW(lnk.c_str());

    // [4] 开机自启（写完立刻删掉）
    wchar_t exe[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    bool set = xgshell::SetAutoStart(L"XGShellDemo", exe);
    std::wstring cmd;
    bool query = xgshell::QueryAutoStart(L"XGShellDemo", &cmd);
    bool removed = xgshell::RemoveAutoStart(L"XGShellDemo");
    std::wprintf(L"[4] autostart set=%d query=%d removed=%d cmd=%s => %s\n",
                 (int)set, (int)query, (int)removed, cmd.c_str(),
                 (set && query && removed) ? L"OK" : L"FAIL");

    // [5] 单实例（同进程内造两个，靠消息窗口验证激活消息能到）
    static int activations = 0;
    xgshell::SingleInstance first(L"xgshell-demo");
    bool primary = first.IsPrimary();
    bool listening = primary && first.Listen([] { ++activations; });
    xgshell::SingleInstance second(L"xgshell-demo");
    bool secondPrimary = second.IsPrimary();
    bool posted = false;
    if (!secondPrimary) posted = second.ActivateExisting();
    // 跑一会儿消息循环把激活消息收下来
    MSG msg;
    DWORD deadline = GetTickCount() + 500;
    while (GetTickCount() < deadline) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(5);
    }
    std::wprintf(L"[5] primary=%d listen=%d secondPrimary=%d posted=%d activations=%d => %s\n",
                 (int)primary, (int)listening, (int)secondPrimary, (int)posted, activations,
                 (primary && !secondPrimary && posted && activations == 1) ? L"OK" : L"FAIL");

    // [6] 权限
    std::wprintf(L"[6] elevated=%d\n", (int)xgshell::IsElevated());
    return 0;
}
