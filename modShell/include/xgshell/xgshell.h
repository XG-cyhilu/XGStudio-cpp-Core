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

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// 前置声明 Windows 类型，公开头里不出现任何 Windows 头
struct HWND__;
using HWND = struct HWND__*;

namespace xgshell {

// ---------------- 单实例 ----------------
//
// 主实例：命名互斥归它所有；后续实例创建失败即知道"已经有了"。
// 已有实例用 Listen() 注册一个只收消息的隐藏窗口，重复启动时用 ActivateExisting()
// 给它投一条自定义消息，回调里一般是"把主窗口拉到前台"。
class SingleInstance {
public:
    explicit SingleInstance(const std::wstring& key);
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    bool IsPrimary() const { return primary_; }

    // 已有实例侧：注册激活回调。必须在该进程的主线程上调用（消息循环所在线程）。
    // 返回 false 表示隐藏窗口创建失败。
    bool Listen(std::function<void()> onActivate);

    // 新实例侧：通知已有实例。找不到窗口时返回 false。
    bool ActivateExisting() const;

    // 解除监听（析构会自动调用）
    void StopListen();

private:
    std::wstring key_;
    bool primary_ = false;
    void* mutex_ = nullptr;       // HANDLE
    HWND hwnd_ = nullptr;
};

// ---------------- 命令行 ----------------
// 按 Windows 规则（双引号 + 连续反斜杠）拆 argv，不依赖 CRT 的 __argv。
std::vector<std::wstring> ParseCommandLine(const wchar_t* cmdLine = nullptr);

// 给参数加引号（含空格/制表/空串时）
std::wstring QuoteArg(const std::wstring& arg);

// ---------------- 关联注册 ----------------
//
// 全部写 HKCU\Software\Classes，不需要管理员权限，可随时用 UnregisterAssociation 撤销。
// progId 举例 "MyCompany.MyApp.Document"；protocol 举例 "myapp"。

// 协议：myapp://foo → "exe" <argsTemplate若空则 "%1">
bool RegisterProtocol(const std::wstring& protocol, const std::wstring& progId,
                      const std::wstring& exePath, const std::wstring& description = L"",
                      const std::wstring& argsTemplate = L"");

// 文件类型：.xyz → progId
bool RegisterFileAssociation(const std::wstring& ext, const std::wstring& progId,
                             const std::wstring& exePath, const std::wstring& description = L"",
                             const std::wstring& iconPath = L"");

bool UnregisterAssociation(const std::wstring& name);     // name = protocol 或 progId
bool QueryAssociationRegistered(const std::wstring& name);

// ---------------- 快捷方式 ----------------
struct Shortcut {
    std::wstring linkPath;      // .lnk 的完整路径
    std::wstring target;        // 目标 exe
    std::wstring arguments;
    std::wstring workDir;
    std::wstring iconPath;
    int iconIndex = 0;
    std::wstring description;
    int showCommand = 1;        // 1 普通窗口 / 3 最大化 / 7 最小化
};

bool CreateShortcut(const Shortcut& sc);
bool ReadShortcut(const std::wstring& linkPath, Shortcut& out);

// ---------------- 开机自启 ----------------
// 写 HKCU\Software\Microsoft\Windows\CurrentVersion\Run，值名 = appName
bool SetAutoStart(const std::wstring& appName, const std::wstring& exePath,
                  const std::wstring& arguments = L"");
bool RemoveAutoStart(const std::wstring& appName);
bool QueryAutoStart(const std::wstring& appName, std::wstring* command = nullptr);

// ---------------- 托盘 ----------------
// 生命周期跟窗口走：调用方在 WM_DESTROY 里 Remove()。
class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    // callbackMsg 会以 NOTIFYICONDATA 的 uCallbackMessage 发到 hwnd（通常是 WM_APP+n）
    bool Add(HWND hwnd, void* hicon, const std::wstring& tooltip, unsigned callbackMsg);
    bool Update(void* hicon, const std::wstring& tooltip);
    bool ShowBalloon(const std::wstring& title, const std::wstring& text, unsigned flags = 0);
    void Remove();

private:
    HWND hwnd_ = nullptr;
    unsigned id_ = 1;
    bool added_ = false;
};

// ---------------- 已知文件夹 ----------------
enum class Folder {
    Desktop,
    Documents,
    Downloads,
    Pictures,
    Music,
    Videos,
    LocalAppData,
    RoamingAppData,
    ProgramData,
    StartMenu,
    StartMenuPrograms,
    Startup,
    Temp,
    Public,
};

// 失败返回空串
std::wstring KnownFolder(Folder id);

// 常用目录拼接：KnownFolder + 子路径
std::wstring AppendPath(const std::wstring& base, const std::wstring& leaf);

// ---------------- Shell 动作 ----------------
bool OpenUrl(const std::wstring& url);
bool OpenFile(const std::wstring& path);
bool RevealInExplorer(const std::wstring& path);   // 打开所在文件夹并选中
bool OpenFolder(const std::wstring& path);

// ---------------- 权限 ----------------
bool IsElevated();
// 以管理员身份重开自己；返回 false 表示用户取消或失败
bool RelaunchElevated(const std::wstring& args = L"");

} // namespace xgshell
