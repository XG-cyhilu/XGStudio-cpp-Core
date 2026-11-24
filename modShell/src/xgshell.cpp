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

#include "xgshell/xgshell.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>

#include <map>
#include <mutex>

namespace xgshell {

namespace {

constexpr UINT kActivateMsg = WM_APP + 7;

bool RegSetSz(HKEY root, const std::wstring& sub, const std::wstring& name,
              const std::wstring& value) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) !=
        ERROR_SUCCESS)
        return false;
    bool ok = RegSetValueExW(k, name.empty() ? nullptr : name.c_str(), 0, REG_SZ,
                             (const BYTE*)value.c_str(),
                             (DWORD)((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}

bool RegGetSz(HKEY root, const std::wstring& sub, const std::wstring& name,
              std::wstring* out) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[1024];
    DWORD type = 0, sz = sizeof(buf);
    bool ok = RegQueryValueExW(k, name.empty() ? nullptr : name.c_str(), nullptr, &type,
                               (BYTE*)buf, &sz) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(k);
    if (ok && out) *out = buf;
    return ok;
}

std::wstring JoinCommand(const std::wstring& exe, const std::wstring& args) {
    std::wstring cmd = QuoteArg(exe);
    if (!args.empty()) cmd += L" " + args;
    return cmd;
}

struct ClassReg {
    std::mutex mu;
    std::map<std::wstring, std::function<void()>> callbacks;
};

ClassReg& Registry() {
    static ClassReg reg;
    return reg;
}

LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kActivateMsg) {
        wchar_t key[256] = {};
        GetWindowTextW(hwnd, key, 255);
        std::function<void()> cb;
        {
            ClassReg& reg = Registry();
            std::lock_guard<std::mutex> lk(reg.mu);
            auto it = reg.callbacks.find(key);
            if (it != reg.callbacks.end()) cb = it->second;
        }
        if (cb) cb();
        return 0;
    }
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::wstring ClassNameForKey(const std::wstring& key) {
    return L"XGShellActivate_" + key;
}

HWND FindMessageWindow(const std::wstring& key) {
    return FindWindowExW(HWND_MESSAGE, nullptr, ClassNameForKey(key).c_str(), nullptr);
}

struct ComInit {
    bool owned = false;
    ComInit() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        owned = SUCCEEDED(hr);
    }
    ~ComInit() {
        if (owned) CoUninitialize();
    }
};

} // namespace

// ---------------- SingleInstance ----------------

SingleInstance::SingleInstance(const std::wstring& key) : key_(key) {
    std::wstring name = L"Local\\xgshell." + key + L".single";
    mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
    primary_ = mutex_ != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
}

SingleInstance::~SingleInstance() {
    StopListen();
    if (mutex_) {
        if (primary_) ReleaseMutex((HANDLE)mutex_);
        CloseHandle((HANDLE)mutex_);
        mutex_ = nullptr;
    }
}

bool SingleInstance::Listen(std::function<void()> onActivate) {
    if (hwnd_) return true;
    if (!onActivate) return false;

    {
        ClassReg& reg = Registry();
        std::lock_guard<std::mutex> lk(reg.mu);
        reg.callbacks[key_] = std::move(onActivate);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MsgWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = ClassNameForKey(key_).c_str();
    RegisterClassExW(&wc);   // 重复注册返回失败，忽略即可

    hwnd_ = CreateWindowExW(0, wc.lpszClassName, key_.c_str(), 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    return hwnd_ != nullptr;
}

bool SingleInstance::ActivateExisting() const {
    HWND target = FindMessageWindow(key_);
    if (!target) return false;
    return PostMessageW(target, kActivateMsg, 0, 0) != FALSE;
}

void SingleInstance::StopListen() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    ClassReg& reg = Registry();
    std::lock_guard<std::mutex> lk(reg.mu);
    reg.callbacks.erase(key_);
}

// ---------------- 命令行 ----------------

std::vector<std::wstring> ParseCommandLine(const wchar_t* cmdLine) {
    std::vector<std::wstring> out;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(cmdLine ? cmdLine : GetCommandLineW(), &argc);
    if (!argv) return out;
    out.reserve((size_t)argc);
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
    LocalFree(argv);
    return out;
}

std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (auto it = arg.begin();;) {
        size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') { ++it; ++backslashes; }
        if (it == arg.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        } else if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(*it);
            ++it;
        } else {
            out.append(backslashes, L'\\');
            out.push_back(*it);
            ++it;
        }
    }
    out.push_back(L'"');
    return out;
}

// ---------------- 关联注册 ----------------

bool RegisterProtocol(const std::wstring& protocol, const std::wstring& progId,
                      const std::wstring& exePath, const std::wstring& description,
                      const std::wstring& argsTemplate) {
    if (protocol.empty() || progId.empty() || exePath.empty()) return false;
    const std::wstring cls = L"Software\\Classes\\";

    std::wstring prog = cls + progId;
    RegSetSz(HKEY_CURRENT_USER, prog, L"", description.empty() ? progId : description);
    std::wstring args = argsTemplate.empty() ? L"%1" : argsTemplate;
    RegSetSz(HKEY_CURRENT_USER, prog + L"\\shell\\open\\command", L"", JoinCommand(exePath, args));

    std::wstring proto = cls + protocol;
    RegSetSz(HKEY_CURRENT_USER, proto, L"", description.empty() ? progId : description);
    RegSetSz(HKEY_CURRENT_USER, proto, L"URL Protocol", L"");
    RegSetSz(HKEY_CURRENT_USER, proto + L"\\DefaultIcon", L"", QuoteArg(exePath) + L",0");
    RegSetSz(HKEY_CURRENT_USER, proto + L"\\shell\\open\\command", L"", JoinCommand(exePath, args));

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool RegisterFileAssociation(const std::wstring& ext, const std::wstring& progId,
                             const std::wstring& exePath, const std::wstring& description,
                             const std::wstring& iconPath) {
    if (ext.empty() || progId.empty() || exePath.empty()) return false;
    std::wstring dotExt = ext[0] == L'.' ? ext : L"." + ext;
    const std::wstring cls = L"Software\\Classes\\";

    std::wstring prog = cls + progId;
    RegSetSz(HKEY_CURRENT_USER, prog, L"", description.empty() ? progId : description);
    RegSetSz(HKEY_CURRENT_USER, prog + L"\\shell\\open\\command", L"",
             JoinCommand(exePath, L"%1"));
    RegSetSz(HKEY_CURRENT_USER, prog + L"\\DefaultIcon", L"",
             QuoteArg(iconPath.empty() ? exePath : iconPath) + L",0");

    RegSetSz(HKEY_CURRENT_USER, cls + dotExt, L"", progId);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool UnregisterAssociation(const std::wstring& name) {
    if (name.empty()) return false;
    bool ok = RegDeleteTreeW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + name).c_str()) ==
              ERROR_SUCCESS;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool QueryAssociationRegistered(const std::wstring& name) {
    return RegGetSz(HKEY_CURRENT_USER, L"Software\\Classes\\" + name, L"", nullptr);
}

// ---------------- 快捷方式 ----------------

bool CreateShortcut(const Shortcut& sc) {
    if (sc.linkPath.empty() || sc.target.empty()) return false;
    ComInit com;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, (void**)&link)))
        return false;

    bool ok = false;
    link->SetPath(sc.target.c_str());
    if (!sc.arguments.empty()) link->SetArguments(sc.arguments.c_str());
    if (!sc.workDir.empty()) link->SetWorkingDirectory(sc.workDir.c_str());
    if (!sc.description.empty()) link->SetDescription(sc.description.c_str());
    if (!sc.iconPath.empty()) link->SetIconLocation(sc.iconPath.c_str(), sc.iconIndex);
    link->SetShowCmd(sc.showCommand);

    IPersistFile* file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void**)&file))) {
        ok = SUCCEEDED(file->Save(sc.linkPath.c_str(), TRUE));
        file->Release();
    }
    link->Release();
    return ok;
}

bool ReadShortcut(const std::wstring& linkPath, Shortcut& out) {
    ComInit com;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, (void**)&link)))
        return false;
    IPersistFile* file = nullptr;
    if (FAILED(link->QueryInterface(IID_IPersistFile, (void**)&file))) {
        link->Release();
        return false;
    }
    bool ok = SUCCEEDED(file->Load(linkPath.c_str(), STGM_READ));
    if (ok) {
        wchar_t buf[MAX_PATH * 2];
        out.linkPath = linkPath;
        if (SUCCEEDED(link->GetPath(buf, MAX_PATH * 2, nullptr, 0))) out.target = buf;
        if (SUCCEEDED(link->GetArguments(buf, MAX_PATH * 2))) out.arguments = buf;
        if (SUCCEEDED(link->GetWorkingDirectory(buf, MAX_PATH * 2))) out.workDir = buf;
        if (SUCCEEDED(link->GetDescription(buf, MAX_PATH * 2))) out.description = buf;
        int idx = 0;
        if (SUCCEEDED(link->GetIconLocation(buf, MAX_PATH * 2, &idx))) {
            out.iconPath = buf;
            out.iconIndex = idx;
        }
    }
    file->Release();
    link->Release();
    return ok;
}

// ---------------- 开机自启 ----------------

namespace {
const std::wstring kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}

bool SetAutoStart(const std::wstring& appName, const std::wstring& exePath,
                  const std::wstring& arguments) {
    if (appName.empty() || exePath.empty()) return false;
    return RegSetSz(HKEY_CURRENT_USER, kRunKey, appName, JoinCommand(exePath, arguments));
}

bool RemoveAutoStart(const std::wstring& appName) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey.c_str(), 0, KEY_WRITE, &k) != ERROR_SUCCESS)
        return false;
    bool ok = RegDeleteValueW(k, appName.c_str()) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}

bool QueryAutoStart(const std::wstring& appName, std::wstring* command) {
    return RegGetSz(HKEY_CURRENT_USER, kRunKey, appName, command);
}

// ---------------- 托盘 ----------------

namespace {
NOTIFYICONDATAW MakeNid(HWND hwnd, unsigned id) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = id;
    return nid;
}
} // namespace

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() { Remove(); }

bool TrayIcon::Add(HWND hwnd, void* hicon, const std::wstring& tooltip, unsigned callbackMsg) {
    if (!hwnd) return false;
    hwnd_ = hwnd;
    NOTIFYICONDATAW nid = MakeNid(hwnd_, id_);
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = callbackMsg;
    nid.hIcon = (HICON)hicon;
    wcsncpy_s(nid.szTip, tooltip.c_str(), _TRUNCATE);
    added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    return added_;
}

bool TrayIcon::Update(void* hicon, const std::wstring& tooltip) {
    if (!added_) return false;
    NOTIFYICONDATAW nid = MakeNid(hwnd_, id_);
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = (HICON)hicon;
    wcsncpy_s(nid.szTip, tooltip.c_str(), _TRUNCATE);
    return Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
}

bool TrayIcon::ShowBalloon(const std::wstring& title, const std::wstring& text, unsigned flags) {
    if (!added_) return false;
    NOTIFYICONDATAW nid = MakeNid(hwnd_, id_);
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = flags ? flags : NIIF_INFO;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
    return Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
}

void TrayIcon::Remove() {
    if (!added_) return;
    NOTIFYICONDATAW nid = MakeNid(hwnd_, id_);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    added_ = false;
}

// ---------------- 已知文件夹 ----------------

std::wstring KnownFolder(Folder id) {
    const GUID* guid = nullptr;
    switch (id) {
    case Folder::Desktop: guid = &FOLDERID_Desktop; break;
    case Folder::Documents: guid = &FOLDERID_Documents; break;
    case Folder::Downloads: guid = &FOLDERID_Downloads; break;
    case Folder::Pictures: guid = &FOLDERID_Pictures; break;
    case Folder::Music: guid = &FOLDERID_Music; break;
    case Folder::Videos: guid = &FOLDERID_Videos; break;
    case Folder::LocalAppData: guid = &FOLDERID_LocalAppData; break;
    case Folder::RoamingAppData: guid = &FOLDERID_RoamingAppData; break;
    case Folder::ProgramData: guid = &FOLDERID_ProgramData; break;
    case Folder::StartMenu: guid = &FOLDERID_StartMenu; break;
    case Folder::StartMenuPrograms: guid = &FOLDERID_Programs; break;
    case Folder::Startup: guid = &FOLDERID_Startup; break;
    case Folder::Public: guid = &FOLDERID_Public; break;
    case Folder::Temp: {
        wchar_t buf[MAX_PATH * 2] = {};
        if (!GetTempPathW(MAX_PATH * 2, buf)) return {};
        return buf;
    }
    default: return {};
    }
    wchar_t* raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(*guid, 0, nullptr, &raw))) return {};
    std::wstring out = raw;
    CoTaskMemFree(raw);
    return out;
}

std::wstring AppendPath(const std::wstring& base, const std::wstring& leaf) {
    if (base.empty()) return leaf;
    if (leaf.empty()) return base;
    std::wstring out = base;
    if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    out += leaf;
    return out;
}

// ---------------- Shell 动作 ----------------

bool OpenUrl(const std::wstring& url) {
    if (url.empty()) return false;
    return (INT_PTR)ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr,
                                 SW_SHOWNORMAL) > 32;
}

bool OpenFile(const std::wstring& path) {
    if (path.empty()) return false;
    return (INT_PTR)ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr,
                                 SW_SHOWNORMAL) > 32;
}

bool RevealInExplorer(const std::wstring& path) {
    if (path.empty()) return false;
    PIDLIST_ABSOLUTE item = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr))) return false;
    bool ok = SUCCEEDED(SHOpenFolderAndSelectItems(item, 0, nullptr, 0));
    ILFree(item);
    return ok;
}

bool OpenFolder(const std::wstring& path) {
    if (path.empty()) return false;
    return (INT_PTR)ShellExecuteW(nullptr, L"open", L"explorer.exe", path.c_str(), nullptr,
                                 SW_SHOWNORMAL) > 32;
}

// ---------------- 权限 ----------------

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elev{};
    DWORD sz = sizeof(elev);
    bool ok = GetTokenInformation(token, TokenElevation, &elev, sz, &sz) != FALSE && elev.TokenIsElevated;
    CloseHandle(token);
    return ok;
}

bool RelaunchElevated(const std::wstring& args) {
    wchar_t exe[MAX_PATH * 2] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH * 2)) return false;
    HINSTANCE h = ShellExecuteW(nullptr, L"runas", exe, args.empty() ? nullptr : args.c_str(),
                                nullptr, SW_SHOWNORMAL);
    return (INT_PTR)h > 32;   // 用户取消授权会返回 SE_ERR_ACCESSDENIED(5)
}

} // namespace xgshell
