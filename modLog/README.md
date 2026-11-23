# XGLOG

Windows 上的 C++17 异步日志库。跟 XGDL 同一套取向：**调用线程不碰 I/O、队列满就丢、UI 只轮询不回调**。

```cpp
#include <xglog/xglog.h>

xglog::Config cfg;
cfg.dir = L"logs";
cfg.level = xglog::Level::Info;      // 控制台/调试器
cfg.fileLevel = xglog::Level::Debug; // 文件更详细
xglog::Init(cfg);

XGLOG_INFO("启动，版本 %s", "1.0.0");
XGLOG_WARN("mods 目录缺失，使用默认");

// 退出前
xglog::Flush();
xglog::Shutdown();
```

## 取向

- **不阻塞调用方**：`XGLOG_*` 只做一次 `vsnprintf` + 一次入队（互斥锁临界区就是 memcpy 级的操作），
  磁盘写入全在唯一写线程里。队列满时直接丢弃并计数（`xglog::Dropped()`），永不等待。
- **内存固定**：队列在 `Init` 时一次性分配 `queueCapacity × messageMax` 的 arena，运行期零分配；
  单条消息默认 1KB，超长按 `_TRUNCATE` 截断。
- **分级过滤在入口做**：低于 `Level` 的调用在加锁前就返回，热路径上只有一次原子读。
- **写入策略**：`Warn` 及以上立即刷盘；普通日志按 `flushIntervalMs`（默认 200ms）批量刷。
  所以「崩溃前最后一条 Error」一定是落盘的，Trace 级别的可能丢最后几百毫秒。
- **轮转**：单文件超过 `maxFileBytes`（默认 8MB）就改名成 `base-YYYYMMDD-HHMMSS-N.log` 并新建，
  只保留 `maxFiles` 个旧文件。每次启动都会把上一轮的 `base.log` 轮转掉，不跨进程追加。
- **UI 轮询**：`xglog::UiPoll(Record&)` 拉取最近 `uiCapacity` 条，跟 XGDL 的 `PollEvent` 一个用法，
  UI 200ms 定时器里循环拉即可。落后超过容量会自动跳到最新，不追历史。

## 输出端

| 开关 | 行为 |
|---|---|
| `console` | 附加控制台时写 stdout，ANSI 分级着色（Trace 灰 / Debug 白 / Info 青 / Warn 黄 / Error 红 / Fatal 红底），
并把控制台输出代码页切到 UTF-8 |
| `debugger` | 有调试器时同时写 `OutputDebugStringA` |
| `file` | 写文件（UTF-8 bytes），默认目录是 exe 同级 `logs/` |

行格式固定为：

```
[2026-11-23 12:45:59.904][D][15676] main.cpp:48 thread 3 line 556 value=278.000
```

`[本地时间含毫秒][级别首字母][线程 ID] 文件名:行号 正文`。

## API

| 接口 | 说明 |
|---|---|
| `Init(const Config&)` | 初始化，重复调用幂等；不调用也能用（首次打日志时懒初始化一份默认配置） |
| `Shutdown()` | 停写线程、刷盘、关文件；之后可再次 `Init` |
| `Flush()` | 等队列排空并 `fflush`（最多自旋约 120ms） |
| `SetLevel` / `GetLevel` | 运行时调控制台与调试器级别（文件级别由 `fileLevel` 固定） |
| `Dropped()` | 队列满丢弃条数，诊断用 |
| `UiPoll(Record&)` | 取一条未消费的 UI 记录，返回 false 表示队空 |
| `Logf / VLogf` | 宏底层，printf 风格格式化 |

宏：`XGLOG_TRACE / DEBUG / INFO / WARN / ERROR / FATAL`，都带 `__FILE__` 与 `__LINE__`。

## 与其它库的关系

- **XGDL 想记日志时不用联动**：XGDL 保持零日志依赖，由调用方在自己的回调/轮询里打 XGLOG。
  两个库都遵守「不在工作线程里推动 UI」这条线，串起来不会互相拖慢。
- 头文件不包含 `windows.h`，只有 `<string>`；调用方工程不会被拖进 Win32 宏污染。

## 构建

```powershell
cmake -S . -B .build -G "Visual Studio 17 2022" -A x64
cmake --build .build --config Release
```

产物 `Release/xglog.lib`、`Release/xglog_demo.exe`。集成：

```cmake
add_subdirectory(path/to/modLog)
target_link_libraries(YourApp PRIVATE XGLOG::xglog)
```

安装形态（`cmake --install`）导出 `xglogConfig.cmake`，`find_package(xglog)` 后同样链接 `XGLOG::xglog`。

## 示例（`xglog_demo`）

1. 4 线程 × 2000 条 Debug 日志压测，打印耗时与吞吐，并核对 `Dropped() == 0`；
2. 追加 1200 条 Trace，配合 `maxFileBytes=256KB` 观察轮转（`maxFiles=4` 之外的旧文件会被删）；
3. 打一条 Error（立即刷盘），然后按 UI 方式 `UiPoll` 一次把最近的 `uiCapacity` 条取出来。

实测（i7-3520M，Release）：8000 条 4 线程 < 130ms 全程无丢弃；1200 条 Trace 触发 2 次轮转。

## 目录

```
modLog/
├─ include/xglog/xglog.h
├─ src/xglog.cpp
├─ demo/main.cpp
└─ CMakeLists.txt
```

## 许可

Apache License 2.0，见仓库根 `LICENSE`。
