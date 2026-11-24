# XGShell

Windows 桌面壳集成的一小块：单实例、协议/文件关联、快捷方式、开机自启、托盘、已知文件夹、
Shell 动作、权限判断。全部只用 Win32 / Shell API，没有第三方依赖，也不假设任何具体产品名。

```cpp
#include <xgshell/xgshell.h>

int wmain() {
    xgshell::SingleInstance inst(L"my-app");        // 互斥名 "Local\\xgshell.my-app.single"
    if (!inst.IsPrimary()) {
        inst.ActivateExisting();                    // 把已有实例拉到前台
        return 0;
    }
    inst.Listen([win] { ShowWindow(win, SW_RESTORE); SetForegroundWindow(win); });

    xgshell::SetAutoStart(L"MyApp", exePath);
    xgshell::RegisterProtocol(L"myapp", L"MyCompany.MyApp.Link", exePath, L"My App");
    ...
}
```

## 能力

| 分组 | API | 说明 |
|---|---|---|
| 单实例 | `SingleInstance(key)` / `IsPrimary()` / `Listen(cb)` / `ActivateExisting()` | 命名互斥 + 消息专用窗口；`Listen` 必须在有消息循环的主线程调用 |
| 命令行 | `ParseCommandLine(cmd = nullptr)` / `QuoteArg(arg)` | 用 `CommandLineToArgvW` 的规则拆参，`QuoteArg` 按 MSVC 规则补引号 |
| 关联 | `RegisterProtocol` / `RegisterFileAssociation` / `UnregisterAssociation` / `QueryAssociationRegistered` | 只写 `HKCU\Software\Classes`，无需管理员；注册完 `SHChangeNotify(SHCNE_ASSOCCHANGED)` |
| 快捷方式 | `CreateShortcut(Shortcut)` / `ReadShortcut(link, Shortcut&)` | `IShellLinkW` + `IPersistFile`，内部按需 `CoInitializeEx` |
| 自启 | `SetAutoStart` / `RemoveAutoStart` / `QueryAutoStart` | 写 `HKCU\...\CurrentVersion\Run`，值名就是 appName |
| 托盘 | `TrayIcon::Add/Update/ShowBalloon/Remove` | 生命周期跟窗口走，`WM_DESTROY` 里 `Remove()` |
| 已知目录 | `KnownFolder(Folder)` / `AppendPath` | `SHGetKnownFolderPath`，Temp 走 `GetTempPathW` |
| Shell 动作 | `OpenUrl` / `OpenFile` / `OpenFolder` / `RevealInExplorer` | 后者等价于"打开所在文件夹并选中该文件" |
| 权限 | `IsElevated()` / `RelaunchElevated(args)` | 后者 `ShellExecuteW(runas)`，用户取消返回 false |

## 设计取舍

- **不注册全局关联**。所有关联写 HKCU，撤销就是删一个键；卸载时调用 `UnregisterAssociation`
  即可，不会留下"打开方式里有但我已经卸载了"的僵尸项。
- **不做自绘托盘**。只包 `Shell_NotifyIconW` 三个动作，图标加载、右键菜单、气球通知的交互逻辑
  留给调用方——它比库更清楚自己有什么菜单。
- **单实例靠互斥而不是窗口查找**。互斥判定不受窗口标题改动、多桌面、权限隔离影响；
  激活通道是消息专用窗口（`HWND_MESSAGE`），不会出现在任务栏或 Alt+Tab 里。
- **公开头零 Windows 依赖**。只前置声明 `HWND`，`HICON` 用 `void*` 传递，调用方不需要先包含
  `windows.h`，也不会被 `min/max` 宏污染。

## 示例

```powershell
xgshell_demo
```

会打印五个已知目录、解析一条含引号的命令行、造一个 `.lnk` 再读回来比对、临时写/删 Run 值、
同进程内造两个 `SingleInstance` 验证激活消息只到一次，最后打印是否提权。

实测（Win10 19044 / Win11 26100 一致）：

| 检查 | 结果 |
|---|---|
| 已知目录 5 项 | 5/5 有值 |
| 快捷方式往返 | `target` / `arguments` 逐字一致 |
| 自启写-查-删 | 三项全 true，注册表无残留 |
| 单实例 | `primary=1 secondPrimary=0 posted=1 activations=1` |

## 目录

```
modShell/
├─ include/xgshell/xgshell.h   唯一公开头
├─ src/xgshell.cpp             Win32/Shell 实现
├─ demo/main.cpp               自检式示例
└─ CMakeLists.txt
```

## 许可

Apache License 2.0，见仓库根目录 `LICENSE`。
