# 变更记录

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循语义化版本。
每个模块目录下另有自己的 README，写的是**怎么用**；这里只记**什么时候加了什么**。

## [1.0.0] — 2026-11-24

首个完整的聚合版本。12 个模块 + 聚合头，全部独立可构建、独立可 `find_package`。

### 新增模块

- **XGSHELL**（`modShell/`）：单实例互斥与"唤起已有窗口"、协议/文件关联（只写 `HKCU\Software\Classes`）、
  `IShellLink` 快捷方式读写、开机自启、托盘图标、已知文件夹、`OpenUrl/RevealInExplorer`、提权重启。
- **XGCRASH**（`modCrash/`）：SEH、`std::terminate`、可选 VEH 三路汇到一份 MiniDump，固定目录 +
  保留最近 N 份 + 写完回调；栈溢出时主动跳过 VEH，避免二次崩溃。
- **XGCORE**（`modCore/`）：聚合头 `<xgcore/xgcore.h>`（`__has_include` 逐个判定）+ 版本宏 +
  运行期版本/构建时间/已编译模块清单。

### 新增能力

- **XGDL**：`TaskOptions`（期望字节数、多种哈希、优先级、镜像列表、请求头、`resume`、`mtimeMs`）；
  `.part` 断点续传（`Range: bytes=N-`，服务端忽略 Range 时自动回退全量重下）；MD5/SHA1/SHA256/SHA512 校验；
  失败时自动轮换下一个镜像；全局限速 `maxSpeedBytesPerSec`；下载完成写回 `mtimeMs`。
- **XGFS**：长路径规范化、磁盘剩余空间、目录总大小、原子目录替换、删除重试。
- **XGSECURE**（新库）：Base64 / Base64Url、密码学随机、HMAC-SHA256、AES-256-GCM 密封、PBKDF2-HMAC-SHA256。
- **XGZIP（写入端）**：`Writer`（`AddFile` / `AddMemory` / `AddDirectory` / `Commit`），store 与固定 Huffman
  deflate 两种方式；`.tmp` 原子提交；UTF-8 文件名；压不小就退回 store。
- **XGNET**：系统代理探测（含 PAC URL）、`Accept-Encoding: gzip` + 自带 gunzip（`Response::decompressed`）、
  URL/query 编解码、cookie jar（`SetCookieFile / DumpCookies / ClearCookies`）。
- **XGCFG**：`Dirty()` 脏标记、`SetOnChange` 变更回调、`Merge`（对象深合并 / 数组与标量整体替换）、
  `Binder` 结构体字段双向绑定（`Pull` 不覆盖默认值、`Push` 只写差异）。
- **XGTHREAD**：`CancelToken`（跨线程协作取消，下载/IO 统一语义）。

### 修复

- XGDL：`MoveFileEx` 与写盘线程抢同一句柄导致改名失败 → `Sink::closed` 标志 + `WaitSinkClosed` 等待收尾。
  此前偶发"文件下完了但 rename failed"就是它。
- XGDL：`curl_multi_*` 非线程安全，多线程同时 Submit 会炸 → `Engine::curlMu_` 把 `add_handle / perform /
  info_read / remove_handle / cleanup` 全部串行化。
- XGNET：确认仓库自带的静态 libcurl 编的是 `CURL_ZLIB=OFF / HTTP_ONLY=ON`，不会自动解 gzip →
  改由库自己 gunzip，README 原"没开压缩"的说明同步修正。

### 新增

- **ctest 套件**：`tests/` 下每个模块一条用例（`-DCPPLOCLIB_BUILD_TESTS=ON` 开启），
  全都不出网 —— 网络相关只打 127.0.0.1；XGDL 直接复用 demo 里的 `--selfcheck`。
  本机 13 条全绿，耗时约 17 秒。
- **GitHub Actions**：`.github/workflows/windows.yml`（`windows-latest` + VS2022 + vcpkg 静态 libcurl，
  二进制包缓存），push / PR / 手动触发都跑 configure → build → ctest。

### 未做（刻意的）

- 不自更新模块、不做安装任务图、不做目录监控 —— 需要产品形态才能定，属上层领域逻辑。
- 不做证书/签名解析 —— 静态 libcurl 已走 schannel，证书校验交给系统。
- 不引 gtest —— 每个用例自带 20 行断言头，省一个依赖，也省一次 `FetchContent`。
