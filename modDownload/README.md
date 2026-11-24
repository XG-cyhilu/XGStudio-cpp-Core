# XGDownload (XGDL)

Windows 上的 C++17 多路复用下载库。网络 I/O 与计算分离：底层是 `libcurl` 的 `CURLM` 事件循环，
上层是大小文件分离的两组线程池，进度通过原子变量对外暴露，UI 只做轮询——不回调、不加锁。

```cpp
#include <xgdl/xgdl.h>

xgdl::Manager mgr;                                   // 默认配置：16 小文件线程 / 4 大文件分块线程

xgdl::TaskOptions opt;
opt.expectBytes = 12345;                             // 0 = 未知，运行时用 Content-Length 补
opt.hash.alg = xgdl::HashAlg::Sha256;                // 摘要算法：Md5 / Sha1 / Sha256 / Sha512
opt.hash.hex = L"…";
opt.mirrors.push_back(L"https://mirror2.example.com/a.zip");   // 主源失败后按顺序换，纯自定义列表
opt.headers.emplace_back(L"Authorization", L"Bearer …");        // 每请求附加头
opt.mtimeMs = 1730000000000;                         // 完成后修文件时间（清单增量比对用）

uint32_t id = mgr.Add(L"https://example.com/a.zip", L"D:\\dl\\a.zip", opt);
mgr.Start();

// UI 每 200ms 触发一次（例如 OnTimer）
auto p = mgr.Snapshot();                             // 字节数、速度、活动/排队/完成/失败计数
xgdl::Event ev;
while (mgr.PollEvent(ev)) { /* 完成或失败，刷新列表 */ }
```

旧的简写 `Add(url, path, expectBytes, expectSha1, priority)` 仍然可用，内部就是一个只填了
`expectBytes` + `Sha1` 的 `TaskOptions`。

## 这一层的设计取向

- **不用 `curl_easy_perform`**。全库只有一个 `CURLM` 句柄，所有请求以 easy handle 挂上去，由单独的
  网络线程跑 `curl_multi_poll`；几千个小文件连续下载时 TCP/TLS 连接自动复用，握手开销摊平。
  `curl_multi_*` 本身不是线程安全的，因此加句柄 / perform / info_read / cleanup 全部走一把内部互斥，
  工作线程只管排队，不会和事件循环抢同一个 multi。
- **回调里不写盘**。`WRITEFUNCTION` 只做一次 memcpy 进每条任务自己的有界队列（默认 1MB），
  队列满了返回 `CURL_WRITEFUNC_PAUSE` 把该 transfer 暂停，写盘线程排空后再由网络线程 `CURLPAUSE_CONT` 续传。
  网络速度不会被慢硬盘拖住，内存也有上限。
- **先写 `.part`，校验通过才改名**。下载过程中永远只碰 `<path>.part`，完成后用
  `MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH)` 一步换到正式位置。目标文件不会出现"写了一半"的中间态，
  崩溃/断电后剩下的 `.part` 正好可以直接续传。
- **断点续传**。重试或重启后如果 `.part` 已有字节，就用 `Range: bytes=N-` 接着写；
  服务器若不认 Range（返回 200 而不是 206），自动丢掉残片退回整段重下，不会把整段内容接在旧尾巴上。
- **镜像回退是通用的**。`TaskOptions::mirrors` 是调用方自己给的一组备用 URL，失败重试时按顺序轮换，
  轮完绕回主源。库里不认识 BMCLAPI、也不认识任何具体站点——加速域名、自建源、回源都只是列表里的一个字符串。
- **大小文件走不同路径**。超过 `largeThreshold`（默认 1MB）的任务按 `chunkSize`（默认 8MB）切块，
  每块一个带 `Range` 的请求，写临时 `.partN` 文件，全部完成后顺序合并成 `.part`、校验整文件摘要、再改名。
  小文件则 16 并发直连直写。
- **校验可按算法配置**。CNG（bcrypt）算文件摘要，支持 MD5 / SHA1 / SHA256 / SHA512；
  同时给了 `expectBytes` 时先比大小，省一次全文件哈希，也能提前抓出服务器截断。
- **零锁轮询**。`Snapshot()` 只读原子计数，不阻塞下载线程；速度是两次采样差值的指数平滑，
  UI 拿到的是稳定值而不是每帧乱跳。
- **失败有明确语义**。HTTP 非 2xx、传输错误、写盘失败、大小/哈希不符 → 退避 1s 重排（顺带换源），
  默认重试 3 次；哈希不符会先删掉坏文件再重下。最终失败通过 `Event` 推给 UI。

## 公开 API

全部接口都在 `include/xgdl/xgdl.h` 里，分三块：类型、`Manager` 方法、两个 `Add` 重载。

### 类型

| 类型 | 内容 |
|---|---|
| `Priority` | `High` / `Normal` / `Low` |
| `HashAlg` | `None` / `Md5` / `Sha1` / `Sha256` / `Sha512`（CNG 实现；`None` = 不校验） |
| `HashExpect` | `{ HashAlg alg; std::wstring hex; }`，十六进制比对、大小写不敏感 |
| `TaskState` | `Queued` / `Downloading` / `Verifying` / `Completed` / `Failed` / `Cancelled` |
| `Progress` | `bytesTotal` / `bytesDone` / `bytesPerSec` / `active` / `queued` / `completed` / `failed` |
| `Event` | `id` / `kind(Completed / Failed)` / `path` / `httpStatus` / `message` |
| `TaskInfo` | `id` / `url` / `path` / `sizeTotal` / `sizeDone` / `state` / `priority` / `attempt` / `large` |

`TaskOptions`（单任务选项）：

| 字段 | 默认 | 说明 |
|---|---|---|
| `expectBytes` | 0 | 期望大小；`0` = 未知，运行中按 `Content-Length` 补齐。给了就先比大小，省一次全文件哈希 |
| `hash` | `{None, L""}` | 摘要算法 + 期望十六进制串 |
| `priority` | `Normal` | 入队优先级 |
| `mirrors` | 空 | 主源失败后**按顺序**尝试的备用 URL；加速域名/自建源/回源都只是字符串，库不认识任何具体站点 |
| `headers` | 空 | 每个请求附加的 UTF-8 键值对（`Authorization` / `Referer` / UA 覆盖） |
| `resume` | `true` | 是否从 `.part` 已有字节续传 |
| `mtimeMs` | 0 | 非 0 时完成后把文件修改时间设成该值（清单增量比对用） |

`Config`（全局参数）：

| 字段 | 默认 | 说明 |
|---|---|---|
| `smallThreads` | 16 | 小文件并发线程数（每线程同时只跑 1 个任务） |
| `largeThreads` | 4 | 大文件分块并发数 |
| `largeThreshold` | 1 MB | 超过这个大小走分块 Range 路径 |
| `chunkSize` | 8 MB | 分块大小 |
| `maxRetries` / `retryDelayMs` | 3 / 1000 | 失败重试次数与退避 |
| `maxConnections` / `maxHostConnections` | 48 / 16 | CURLM 总连接数 / 单主机连接上限 |
| `ringBytes` | 1 MB | 每任务接收环形缓冲；写满即暂停该 easy handle |
| `writeThreads` | 2 | 独立写盘线程数 |
| `connectTimeoutSec` / `idleTimeoutSec` | 15 / 30 | 连接超时、空闲超时 |
| `maxSpeedBytesPerSec` | 0 | 全局限速（字节/秒），`0` = 不限；多任务时是**每任务**上限 |

### `Manager`

| 方法 | 说明 |
|---|---|
| `Manager(const Config& = {})` | 不可拷贝；析构自动 `Shutdown()` |
| `Add(url, path, TaskOptions)` → `uint32_t` | 完整入队，返回任务 id；空 URL / 空路径 / 路径重复返回 `0` |
| `Add(url, path, expectBytes, expectSha1, priority)` → `uint32_t` | 简写重载，内部就是只填 `expectBytes` + `Sha1` 的 `TaskOptions` |
| `Start()` | 启动调度与线程 |
| `Pause()` | 只停止派发新任务，在跑的继续 |
| `Resume()` | 恢复派发 |
| `Cancel(id)` / `CancelAll()` | 取消单任务 / 全部（清三队列并置 `Cancelled`，不留 part 残片） |
| `Snapshot()` → `Progress` | UI 每帧或每 200ms 调一次；`bytesPerSec` 按两次调用的间隔算（指数平滑） |
| `PollEvent(Event&)` → `bool` | 轮询完成/失败事件，非阻塞；`false` = 队列空 |
| `QueryTask(id, TaskInfo&)` → `bool` | 单任务状态、已下载字节、重试次数、是否走分块 |
| `WaitIdle()` | 阻塞到无排队与在途任务（CLI/测试用） |
| `Shutdown()` | 停线程并清理；之后不再可用 |

### 调用约定

- **模型是轮询，没有回调**：进度拉 `Snapshot()`、结果拉 `PollEvent()`，与 UI 线程每 200ms 拉一次的设计一致。
- **线程**：`Add` / `Cancel` / `Snapshot` / `PollEvent` / `QueryTask` 都可从 UI 线程调；`curl_multi_*` 已在库内用一把互斥串行化，不要自己在外面并发碰底层。
- 公开头**不包含 `curl.h`、也不包含任何 Windows 头**，只有标准库，调用方工程不会被 libcurl 的宏污染。
## 构建

静态 libcurl 需要先准备（本仓库默认找 `.deps/curl-install`，也可以用 `-DXGDL_CURL_DIR=...` 指别处，
或者删掉这个缓存变量让它走 `find_package(CURL)`）：

```powershell
# libcurl 8.11.1 + Schannel（Windows 原生 TLS，不引入 OpenSSL）
curl -L -o .deps/curl.tar.gz https://curl.se/download/curl-8.11.1.tar.gz
tar -xf .deps/curl.tar.gz -C .deps
cmake -S .deps/curl-8.11.1 -B .deps/curl-build -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_CURL_EXE=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF `
  -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF -DUSE_NGHTTP2=OFF `
  -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DHTTP_ONLY=ON `
  -DCMAKE_INSTALL_PREFIX=.deps/curl-install
cmake --build .deps/curl-build --config Release
cmake --install .deps/curl-build --config Release

# 本库
cmake -S . -B .build -G "Visual Studio 17 2022" -A x64
cmake --build .build --config Release
```

产物：`Release/xgdl.lib`、`Release/xgdl_demo.exe`。

## 集成

```cmake
add_subdirectory(path/to/modDownload)
target_link_libraries(YourApp PRIVATE XGDL::xgdl)
```

`XGDL::xgdl` 会传递 libcurl 静态库与 `bcrypt ws2_32 crypt32 secur32 wldap32`，并给调用方带上 `c++std_17`。

安装形态（`cmake --install`）会导出 `xgdlConfig.cmake`，可以 `find_package(xgdl)` 后链接 `XGDL::xgdl`：

```powershell
cmake --install .build --config Release --prefix D:\SDK\xgdl
```

```cmake
find_package(xgdl 1.0 REQUIRED CONFIG)
target_link_libraries(YourApp PRIVATE XGDL::xgdl)
```

安装导出里写的 curl 依赖取决于配置时用的是哪一种：走 `find_package(CURL)`（vcpkg / 系统前缀）时导出
`CURL::libcurl`，消费端需要 `find_package(CURL)` 能解析到静态 curl；用 `.deps/curl-install` 私有目录时
会导出那条绝对路径，只适合本机。跨机器分发建议前者。

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="D:\SDK\xgdl;D:\SDK\curl"
```

如果消费者手上就是同一份 `.deps/curl-install`（或干脆同仓库），用 `add_subdirectory` 形态更省事。

## 示例

```powershell
xgdl_demo <url> <保存路径> [sha1] [预计字节数]
xgdl_demo --batch list.txt          # 每行 "URL<TAB>保存路径"
xgdl_demo --selfcheck               # 本地回环自检，不需要外网
```

`预计字节数` 是触发分块路径的依据：给了且超过 1MB 就会切块并发 Range 下载，给 0 表示未知（单流）。
批量模式用 16 个小文件线程，适合验证连接复用。

`--selfcheck` 会在 `127.0.0.1` 上起一个只够自检用的极小 HTTP 服务（`/bad` 返回 404、其它按 `Range`
返回 206），四组断言跑完打印 `OK/FAIL`：镜像回退（主 404 → 镜像 200 + 请求头）、断点续传
（残留 120KB `.part` → 服务器看到 `Range: bytes=120000`）、摘要不符（必须失败且删文件）、
分块合并（512KB 分块 × 6 合并后 SHA-256 与参考一致）。适合当回归用。

实测（本机）：

| 场景 | 结果 |
|---|---|
| 4.17MB 包，2 个 Range 分块，带 SHA1 校验 | 1.15s，哈希一致，无残留 part 文件 |
| 200 个 250KB 文件，16 并发 | 2.8s，200/200 成功 |
| 404 地址 | 重试 3 次（约 3s）后失败并抛出事件 |
| SHA1 故意写错 | 校验失败 → 删文件 → 重试 3 次 → 最终失败 |
| `--selfcheck`（镜像 / 续传 / 摘要 / 分块） | 4/4 OK，`attempt=1`（主源 404 后第一次换源即成功） |
| 300KB 单流 + 120KB 残片 | 服务器收到 `Range: bytes=120000`，只补传剩余 187200 字节 |
| GitHub release 安装包 65.9MB（4 路分块 + SHA-256） | 12.74s，平均 5.3MB/s、峰值 11.0MB/s；文件 SHA-256 与 release 官方值逐字符一致 |

## 目录

```
modDownload/
├─ include/xgdl/xgdl.h     唯一公开头（TaskOptions / HashAlg 也在这里）
├─ src/
│  ├─ internal.h            Job / Sink / Engine 声明（内部）
│  ├─ Engine.cpp            CURLM 事件循环 + 写盘线程 + 背压暂停 + 请求头/限速
│  ├─ Manager.cpp           分类、分块、镜像轮换、续传、合并、校验、重试、进度快照
│  └─ Util.cpp             UTF-8/宽字符、递归建目录、CNG 多算法摘要、改名、修 mtime
├─ demo/main.cpp           单任务 / 批量 / --selfcheck 三种模式
└─ CMakeLists.txt
```

## 许可

Apache License 2.0，见仓库根目录 `LICENSE`。
