# XGStudio-cpp-Core

XGstudio 的 C++ 公共组件聚合库（本地目录名仍为 `cpploclib`）。每个子目录是一个可以单独拿走、单独构建的库，共享同一套
Apache-2.0 许可与构建约定（C++17、MSVC `/utf-8`、UTF-8 BOM 源码、CMake `find_package` 导出）。

主要用于 [Void Craft Launcher](https://github.com/XG-cyhilu/Void-Craft-UI) 及其生态，
但模块之间互不依赖，也可以在任何 Windows C++17 工程里直接用。

## 模块

| 目录 | 库 | CMake 目标 | 说明 |
|---|---|---|---|
| `modDownload/` | XGDL | `XGDL::xgdl` | libcurl `CURLM` 多路复用 + 大小文件分离线程池的下载库：分块 Range、镜像回退、`.part` 断点续传、MD5/SHA1/SHA256/SHA512 校验、请求头、原子进度轮询及 UI 事件 |
| `modLog/` | XGLOG | `XGLOG::xglog` | 异步日志：固定内存环形队列、队列满即丢并计数、分级过滤、文件轮转、控制台着色、UI 轮询拉取 |
| `modProc/` | XGPROC | `XGPROC::xgproc` | 通用子进程：Job 对象托管整棵进程树、stdout/stderr 按行回收、轮询取行、stdin 写入、超时与退出码、OEM/UTF-8 自动转码 |
| `modCrypto/` | XGCRYPTO | `XGCRYPTO::xgcrypto` | 摘要计算：MD5/SHA1/SHA256/SHA384/SHA512 的增量与一次性接口、文件哈希、CRC 校验配套的定长比较（CNG 实现） |
| `modZip/` | XGZIP | `XGZIP::xgzip` | zip 读取与解压：中央目录/ZIP64 解析、自带 inflate（store/fixed/dynamic）、filter 过滤、CRC+size 校验、路径穿越防护、atomic 进度 |
| `modFs/` | XGFS | `XGFS::xgfs` | 文件与路径：规范化/拆分、原子写、临时文件、递归遍历（通配过滤）、复制/移动/安全删除 |
| `modCfg/` | XGCFG | `XGCFG::xgcfg` | JSON 值树与配置文件：宽松解析（注释/尾逗号）、保持插入顺序的对象、点路径读写、默认值、原子保存、mtime 变更检测与重载 |
| `modThread/` | XGTHREAD | `XGTHREAD::xgthread` | 线程池（future / fire-and-forget / WaitIdle）与主线程调度队列（任意线程 Post、拥有者线程 RunPending） |
| `modNet/` | XGNET | `XGNET::xgnet` | 同步 HTTP 客户端（libcurl easy + 连接复用）：请求头/体、重定向、超时、代理、状态码与响应头；安装时自带静态 libcurl |
| `modSecure/` | XGSECURE | `XGSECURE::xgsecure` | 编解码与密钥材料（CNG）：Base64 / Base64Url、密码学随机、HMAC-SHA256、AES-256-GCM 密封、PBKDF2-HMAC-SHA256 |
| `modShell/` | XGSHELL | `XGSHELL::xgshell` | Windows 桌面壳集成：单实例互斥 + 激活已有窗口、协议/文件关联（HKCU）、IShellLink 快捷方式、开机自启、托盘、已知文件夹、资源管理器动作、提权重启 |
| `modCrash/` | XGCRASH | `XGCRASH::xgcrash` | 崩溃转储：SEH / `std::terminate` / 可选 VEH 三路汇到 `MiniDumpWriteDump`，固定目录 + 保留最近 N 个 + 写完回调 |

后续模块在此表登记。

## 依赖

统一走 [vcpkg](https://github.com/microsoft/vcpkg)（`vcpkg.json` 已声明），或按各模块 README 自行
准备静态依赖。XGDL 需要的静态 libcurl 必须开 `schannel`，不需要 OpenSSL / nghttp2 / zlib。

```powershell
vcpkg install --triplet x64-windows-static
cmake -S . -B .build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build .build --config Release
```

## 集成

```cmake
add_subdirectory(third_party/cpploclib)
target_link_libraries(YourApp PRIVATE XGDL::xgdl XGLOG::xglog XGPROC::xgproc
                                        XGCRYPTO::xgcrypto XGSECURE::xgsecure XGZIP::xgzip
                                        XGFS::xgfs XGCFG::xgcfg XGTHREAD::xgthread XGNET::xgnet
                                        XGSHELL::xgshell XGCRASH::xgcrash)
```

按需关掉不需要的模块：`-DCPPLOCLIB_BUILD_LOG=OFF`、`-DCPPLOCLIB_BUILD_PROC=OFF`、
`-DCPPLOCLIB_BUILD_CRYPTO=OFF`、`-DCPPLOCLIB_BUILD_SECURE=OFF`、`-DCPPLOCLIB_BUILD_ZIP=OFF`、
`-DCPPLOCLIB_BUILD_FS=OFF`、`-DCPPLOCLIB_BUILD_CFG=OFF`、`-DCPPLOCLIB_BUILD_THREAD=OFF`、
`-DCPPLOCLIB_BUILD_NET=OFF`、`-DCPPLOCLIB_BUILD_SHELL=OFF`、`-DCPPLOCLIB_BUILD_CRASH=OFF`，
或者只加某一个模块：`add_subdirectory(third_party/cpploclib/modSecure)`，效果相同。

## 许可

Apache License 2.0，全文见 [LICENSE](LICENSE)。每个源文件头部都带 SPDX 标识。
