# XGDownload (XGDL)

Windows 上的 C++17 多路复用下载库。网络 I/O 与计算分离：底层是 `libcurl` 的 `CURLM` 事件循环，
上层是大小文件分离的两组线程池，进度通过原子变量对外暴露，UI 只做轮询——不回调、不加锁。

```cpp
#include <xgdl/xgdl.h>

xgdl::Manager mgr;                                   // 默认配置：16 小文件线程 / 4 大文件分块线程
uint32_t id = mgr.Add(L"https://example.com/a.zip", L"D:\\dl\\a.zip",
                      0,                                 // 未知大小
                      L"",                               // 不校验
                      xgdl::Priority::High);
mgr.Start();

// UI 每 200ms 触发一次（例如 OnTimer）
auto p = mgr.Snapshot();                             // 字节数、速度、活动/排队/完成/失败计数
xgdl::Event ev;
while (mgr.PollEvent(ev)) { /* 完成或失败，刷新列表 */ }
```

## 这一层的设计取向

- **不用 `curl_easy_perform`**。全库只有一个 `CURLM` 句柄，所有请求以 easy handle 挂上去，由单独的
  网络线程跑 `curl_multi_poll`；几千个小文件连续下载时 TCP/TLS 连接自动复用，握手开销摊平。
- **回调里不写盘**。`WRITEFUNCTION` 只做一次 memcpy 进每条任务自己的有界队列（默认 1MB），
  队列满了返回 `CURL_WRITEFUNC_PAUSE` 把该 transfer 暂停，写盘线程排空后再由网络线程 `CURLPAUSE_CONT` 续传。
  网络速度不会被慢硬盘拖住，内存也有上限。
- **大小文件走不同路径**。超过 `largeThreshold`（默认 1MB）的任务按 `chunkSize`（默认 8MB）切块，
  每块一个带 `Range` 的请求，写临时 `.partN` 文件，全部完成后顺序合并并校验整文件哈希；
  小文件则 16 并发直连直写。
- **零锁轮询**。`Snapshot()` 只读原子计数，不阻塞下载线程；速度是两次采样差值的指数平滑，
  UI 拿到的是稳定值而不是每帧乱跳。
- **失败有明确语义**。HTTP 非 2xx、传输错误、写盘失败、哈希不符 → 计数 +1，退避 1s 重排，
  默认重试 3 次；哈希不符会先删掉坏文件再重下。最终失败通过 `Event` 推给 UI。

## 公开 API

| 类型 | 说明 |
|---|---|
| `Config` | 线程数、阈值、分块大小、重试次数、连接上限、环形缓冲大小、超时 |
| `Manager::Add(url, path, expectBytes, sha1, priority)` | 入队，返回任务 id（0 = 参数非法/路径重复） |
| `Manager::Start / Pause / Resume / Cancel / CancelAll` | 生命周期 |
| `Manager::Snapshot()` | `Progress`：bytesTotal / bytesDone / bytesPerSec / active / queued / completed / failed |
| `Manager::PollEvent(Event&)` | UI 轮询完成/失败，非阻塞 |
| `Manager::QueryTask(id, TaskInfo&)` | 单任务状态、已下载字节、重试次数 |
| `Manager::WaitIdle()` | 跑到队列空（CLI/测试） |
| `Manager::Shutdown()` | 停线程；析构自动调用 |

公开头 `include/xgdl/xgdl.h` **不包含 `curl.h`，也不包含任何 Windows 头**，只有标准库 + 前置声明，
所以调用方工程不会被拖进 libcurl 的宏污染。

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

安装后的导入目标是干净的——libcurl 不再是绝对路径，而是 `CURL::libcurl`。所以消费者侧必须先让
`find_package(CURL)` 找到静态 curl：vcpkg 装到全局前缀即可，或把 curl 安装前缀放进 `CMAKE_PREFIX_PATH`：

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="D:\SDK\xgdl;D:\SDK\curl"
```

如果消费者只有 `.deps/curl-install` 这种私有目录，用 `add_subdirectory` 形态更省事。

## 示例

```powershell
xgdl_demo <url> <保存路径> [sha1] [预计字节数]
xgdl_demo --batch list.txt          # 每行 "URL<TAB>保存路径"
```

`预计字节数` 是触发分块路径的依据：给了且超过 1MB 就会切块并发 Range 下载，给 0 表示未知（单流）。
批量模式用 16 个小文件线程，适合验证连接复用。

实测（本机，1000M 内网）：

| 场景 | 结果 |
|---|---|
| 4.17MB 包，2 个 Range 分块，带 SHA1 校验 | 1.15s，哈希一致，无残留 part 文件 |
| 200 个 250KB 文件，16 并发 | 2.8s，200/200 成功 |
| 404 地址 | 重试 3 次（约 3s）后失败并抛出事件 |
| SHA1 故意写错 | 校验失败 → 删文件 → 重试 3 次 → 最终失败 |

## 目录

```
modDownload/
├─ include/xgdl/xgdl.h     唯一公开头
├─ src/
│  ├─ internal.h            Job / Sink / Engine 声明（内部）
│  ├─ Engine.cpp            CURLM 事件循环 + 写盘线程 + 背压暂停
│  ├─ Manager.cpp           分类、分块、合并、校验、重试、进度快照
│  └─ Util.cpp             UTF-8/宽字符、递归建目录、CNG SHA1
├─ demo/main.cpp           单任务 / 批量两种模式
└─ CMakeLists.txt
```

## 许可

Apache License 2.0，见仓库根目录 `LICENSE`。
