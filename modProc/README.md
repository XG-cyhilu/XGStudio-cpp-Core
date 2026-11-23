# XGPROC

通用子进程执行与输出捕获（Windows / C++17 / 静态库 / Apache-2.0）。

跑一个命令行、把 stdout 和 stderr 按行收回来、轮询取用、随时收掉整棵进程树。**不带任何语义**——不认 Java、不认游戏、不认启动器，任何要"起个外部程序并看它说什么"的场合都能用。

```cpp
#include <xgproc/xgproc.h>

auto r = xgproc::Run(L"cmd /c echo hello && exit 3", 10'000);
// r.started == true, r.exitCode == 3, r.out[0].text == "hello"
```

## 为什么是轮询而不是回调

和 XGDL / XGLOG 同一套取向：**输出进有界队列，调用方自己来取**。

- 不注册回调 → 不用管线程安全、不用管重入、不用管回调里再调库；
- 队列有上限 → 下游不读也不会把内存吃爆，满了就丢并计数（`Dropped()`）；
- 读线程永远在读 → 哪怕下游完全不读，也不会因为管道写满把子进程卡死。

对启动器/构建系统这类"UI 线程抽空看一眼日志"的场景，这是最省心的形状。

## API

### 一次性跑完

```cpp
xgproc::Result r = xgproc::Run(L"cmd /c dir", /*timeoutMs=*/30'000, /*workDir=*/L"");
```

`Result { started, timedOut, exitCode, dropped, out }`，`out` 是 **stdout 与 stderr 按到达顺序合并**的行数组，每行带 `stderrLine` 标记。超时到点会连带子孙一起杀掉，`timedOut = true`。

### 长驻进程

```cpp
xgproc::Process p;
xgproc::Options o;
o.commandLine = L"ping.exe -n 100 127.0.0.1";
o.workDir = L"D:\\tmp";                                  // 空 = 继承
o.env = { { L"PATH", L"C:\\extra;%PATH%" } };            // 覆盖/追加，值里可用 %VAR%
o.queueCapacity = 4096;                                  // 输出行上限
o.lineBytes = 8 * 1024;                                  // 单行上限，超出强制断行
o.outputEncoding = xgproc::Encoding::Auto;               // 见下
p.Start(o);

xgproc::Line ln;
while (p.Poll(ln, 100)) {                                // 100ms 等不到就返回 false
    if (ln.stderrLine) fprintf(stderr, "%s\n", ln.text.c_str());
}

p.WriteStdin("y\n");                                     // 回答交互式提示
p.Stop(1500);                                            // 先等 1.5s，再 TerminateJobObject
```

| 成员 | 说明 |
| --- | --- |
| `Start(Options)` / `Start(cmd, workDir)` | 一个实例只起一次；失败返回 false |
| `Poll(Line&, timeoutMs)` | 取一行，`0` = 不等待 |
| `WriteStdin(string / wstring)` | 写子进程 stdin（宽字符自动转 UTF-8） |
| `IsRunning` / `Wait` / `ExitCode` / `Pid` / `Dropped` | 状态查询，`ExitCode` 未退出时是 `INT32_MIN` |
| `Stop(graceMs)` | `TerminateJobObject`，子孙一个不留 |
| `Drain(timeoutMs)` | 读完剩余输出 + 等退出，返回 `Result` |

### 命令行拼装

```cpp
xgproc::JoinArgs({ L"C:\\Program Files\\a.exe", L"--x=\"y z\"", L"tail\\" })
// "C:\Program Files\a.exe" "--x=\"y z\"" tail\
```

按 `CommandLineToArgvW` 规则转义：需要时才加引号，引号前的反斜杠翻倍，结尾反斜杠不吞收尾引号。**只拼参数，不拼 `cmd.exe` 的 `^` 转义**——那套规则不一样，要过 shell 请自己写。

## 输出编码

Windows 控制台程序吐出来的字节五花八门：cmd / Java / 各种工具多是系统 ANSI/OEM（简体中文就是 936），也有不少直接写 UTF-8。`Encoding` 决定怎么变成 `Line.text`：

| 取值 | 行为 |
| --- | --- |
| `Auto`（默认） | 先做严格 UTF-8 校验；不合法就按 `GetOEMCP()` 转 |
| `Utf8` | 原样保留字节 |
| `Oem` / `Ansi` | 强制按 `GetOEMCP()` / `GetACP()` 转换 |

`Auto` 的取舍是：合法 UTF-8 的字节序列极少恰好也是合法的 GBK，所以先按 UTF-8 试、失败再退回 OEM，实测中英文混排的工具输出都能正确落地。遇到乱码就显式指定 `Oem` 或 `Utf8`。

## 进程树

子进程一创建就 `AssignProcessToJobObject`，Job 设 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`。所以：

- `Stop()` 或 `Process` 析构 → 整棵子孙树一起死，不留孤儿（`cmd /c start ...` 起的孙子也跑不掉）；
- 父进程被强杀时 Job 句柄随之关闭 → 系统替你收尸。

## 边界

- **Windows only**（Job 对象、`CreateProcessW`）。别的平台要另写实现。
- 不接管子进程的 stdin 之外的一切交互：没有 PTY，没有伪终端，交互式全屏程序的输出会不好看。
- `Poll` 是单消费者模型，多线程同时取行不保证顺序。
- 不做输出落盘，要存档就 `Line` 转手丢给 XGLOG。

## 构建

独立 configure：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

作为子目录：

```cmake
set(XGPROC_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cpploclib/modProc)
target_link_libraries(app PRIVATE XGPROC::xgproc)
```

安装后消费：

```powershell
cmake --install build --prefix C:\libs\xgproc --config Release
```

```cmake
find_package(xgproc 1.0 REQUIRED CONFIG)
target_link_libraries(app PRIVATE XGPROC::xgproc)
```

依赖只有系统库 `synchronization`（`WaitOnAddress`），没有第三方。

## Demo 实测（`.build\modProc\Release\xgproc_demo.exe`）

| 项 | 结果 |
| --- | --- |
| 一次性执行 `echo` + `exit 3` | `started=1 exit=3`，两行按序收回 |
| stderr 分离 | stdout 1 行 / stderr 1 行，`stderrLine` 正确 |
| 环境变量注入 `XGPROC_MARK` | 子进程读到 `MARK=injected-42` |
| 长驻轮询 + 1.2s 后 `Stop` | 收到行后 `running=0`，子孙被杀，`dropped=0` |
| 引号转义 | `"C:\a b\c.exe" "--x=\"y z\"" tail\` |
| 1.5s 超时跑 20 次 ping | `timedOut=1`，剩余输出中文按 OEM 转 UTF-8 正确显示 |

`cmake --install` → 外部工程 `find_package(xgproc 1.0 REQUIRED CONFIG)`：configure / build / run 全通（`exit=0`，输出 `installed-ok`）。

> 公开头与源码均为 UTF-8 **带 BOM**：外部 MSVC 工程不加 `/utf-8` 时按 936 解析，无 BOM 会直接把中文注释当语法错误（C2059/C2143）。
