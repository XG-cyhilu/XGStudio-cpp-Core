# XGTHREAD

线程池 + 主线程调度队列 + 协作取消（Windows / C++17 / 静态库 / Apache-2.0），无第三方依赖。

```cpp
#include <xgthread/xgthread.h>

xgthread::Pool pool;                       // 默认 = 逻辑核数
auto fut = pool.Submit([url] { return Fetch(url); });   // 带回值
pool.Post([&] { counter++; });              // fire-and-forget
int v = fut.get();                          // 需要时再等
pool.WaitIdle(5000);

xgthread::MainQueue ui;                     // UI/渲染线程持有
worker.Post([&] { ui.Post([&] { label.SetText(L"done"); }); });
// UI 线程每帧：
ui.RunPending();
```

## 为什么是这两个，而不是一套调度器

启动器/游戏客户端里真正重复出现的是两种需求：

1. **后台干活**：解析元数据、算哈希、解压、HTTP。要能并发、要能拿结果、要知道什么时候全干完。
2. **回到主线程**：后台干完了要改 UI 对象。UI 对象不是线程安全的，所以必须投回创建它的那条线程。

两者职责不同，混成一个 API 反而到处是"这个回调在哪个线程"的注释。分成 `Pool` 和 `MainQueue`，边界一眼就能看出来。

**刻意不做**：优先级、work-stealing、定时器。取消不做抢占（`TerminateThread` 只会留下半截文件），而是给一个协作式 `CancelToken`，长任务自己检查。

## Pool

| 接口 | 说明 |
| --- | --- |
| `Pool(threads = 0)` | `0` = `HardwareThreads()`（按进程亲和位图数） |
| `Submit(f) → std::future<R>` | 要返回值；`f` 抛异常时 `future.get()` 会 rethrow |
| `Post(f) → bool` | fire-and-forget；池已关闭返回 `false` |
| `Threads / Pending` | 线程数 / 排队未开始的个数 |
| `WaitIdle(ms)` | 队列空且无执行中任务 |
| `Shutdown(drain)` | `drain=true` 先跑完队列；析构自动调用 |

> 注意 `Post` 与 `Submit` 是分开的两个名字。早期版本把 fire-and-forget 也叫 `Submit(std::function<void()>)`，结果带返回值的 lambda 会被这个非模板重载抢走（`std::function<void()>` 接受任何可调用对象，只是丢弃返回值），编译期直接选错重载。改名之后模板永远命中。

任务里再 `Submit` 是允许的（demo 第 7 项就是菱形提交），前提是池里还有线程能跑内层任务——池是固定大小，全被外层占满又没有内层进展时会等住，这是所有固定线程池的通病。

## MainQueue

| 接口 | 说明 |
| --- | --- |
| `Post(f)` | 任意线程投递，线程安全 |
| `RunPending(max)` | 拥有者线程执行，最多 `max` 个；返回执行数 |
| `Size / Clear` | 排队数 / 丢弃未执行任务 |

`RunPending` 先把一批任务挪出队列、**释放锁再执行**，所以任务内部可以继续 `Post`（下一帧执行），不会自锁。每帧调用一次即可，也可以限个数（比如 16）来保证帧时间。

## CancelToken（协作取消）

```cpp
auto tok = xgthread::CancelToken();
pool.Post([tok] {
    for (auto& chunk : chunks) {
        tok.ThrowIfCancelled();          // 抛 xgthread::Cancelled
        DownloadChunk(chunk);
    }
});
tok.Cancel();                            // UI 线程点"取消"，立刻生效
```

| 接口 | 说明 |
| --- | --- |
| `CancelToken()` | 新建一个独立令牌（`Cancel()` 前永远不取消） |
| 拷贝 / 赋值 | 副本共享同一份状态，任意一份 `Cancel()` 全体生效 |
| `Cancel()` | 幂等；同时向下递归取消所有子令牌 |
| `Cancelled()` / `ThrowIfCancelled()` | 轮询 / 抛 `xgthread::Cancelled`（`Pool::Submit` 的 future 会把这个异常带出来） |
| `WaitCancel(ms)` | 等最多 `ms`，期间被取消返回 `true`（避免忙等轮询） |
| `Child(parent)` | 父被取消时子一起取消；取消子不影响父 |

用法约定：**谁发起谁持有根令牌**，把副本（或 `Child`）交给任务；任务里只在"可中断点"检查（每循环一次、每写一块、每次 HTTP 回调），不做抢占式中断。XGDL / XGZIP 的取消语义就是这样落地的。

## 实测（`xgthread_demo.exe`）

| 项 | 结果 |
| --- | --- |
| 逻辑核数 | 4（本机按亲和位图统计） |
| 64 个分块任务并行求和 | `2047968000`，与期望值一致 |
| 500 个 fire-and-forget + `WaitIdle` | `hits=500 pending=0` |
| 任务抛异常 | `future.get()` 捕获到 `boom`，线程池不崩 |
| 20 个 worker 投递到 MainQueue，主线程 `RunPending` | `onMain=20 wrongThread=0`（全部在调用线程执行） |
| `Shutdown` 后 `Post` | 返回 0（拒绝） |
| 任务内再提交 4 个任务（4 线程池） | 内层全部完成，`nested sum=60` |
| 200 步循环接到取消（第 3 步停） | `future.get()` 抛 `Cancelled`；子令牌同步变为已取消 |

## 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

```cmake
set(XGTHREAD_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cpploclib/modThread)
target_link_libraries(app PRIVATE XGTHREAD::xgthread)
```

安装后：`find_package(xgthread 1.0 REQUIRED CONFIG)` + `XGTHREAD::xgthread`。

> 公开头与源码均为 UTF-8 带 BOM（外部 MSVC 工程不加 `/utf-8` 时按 936 解析，中文注释会直接编译失败）。
