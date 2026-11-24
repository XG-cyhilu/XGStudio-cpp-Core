# XGCORE

聚合头 + 版本信息（Windows / C++17 / 静态库 / Apache-2.0 / 无第三方依赖）。

它不做任何业务，只解决两件小事：

1. **一个头拿全**：`<xgcore/xgcore.h>` 用 `__has_include` 收拢本次构建里存在的模块头，缺哪个就跳过，所以关掉某个模块也不会编译失败；
2. **版本与构建信息**：版本宏、运行期版本字符串、库编译时间、实际编进来的模块清单。

```cpp
#include <xgcore/xgcore.h>

xglog::Info("core {} built {}", xgcore::VersionString(), xgcore::BuildTimestamp());

// 兼容判断：调用方要求 >= 1.0.0
xgcore::Version v = xgcore::GetVersion();
if (v.major < 1) { /* 太老 */ }

// 打一条启动日志，看看这次到底带了哪些能力
for (const auto& m : xgcore::Modules()) xglog::Info("module: {}", m);
```

## 接口

| 接口 | 说明 |
| --- | --- |
| `XGCORE_VERSION_MAJOR/MINOR/PATCH/STRING` | 编译期版本宏 |
| `xgcore::VersionString()` | 运行期版本字符串，`1.0.0` |
| `xgcore::GetVersion()` | 结构体版本，逐字段比较比解析字符串省事 |
| `xgcore::BuildTimestamp()` | 本库的 `__DATE__ __TIME__`，排查"装的是哪次构建"用 |
| `xgcore::Modules()` | 本次构建实际编进来的模块名（字母序，`xgdl`、`xgcfg`…） |

`Modules()` 的清单是 CMake 构建期生成的（`xgcore_modules.h`），按 `if (TARGET XGDL::xgdl)` 判断，
所以关掉模块后清单也跟着变，不会说谎。

## 取舍

- **不聚合到"一个 lib 全搞定"**：各模块仍是独立静态库。`XGCORE::xgcore` 只是 PUBLIC 链接它们，方便拿 include 目录；
- **不做版本兼容开关**：没有 `XGCORE_VERSION_MAJOR` 的 ABI 分支宏。库还在 0→1 的推进期，加这种宏反而固化错误；
- **不做运行期初始化**：没有 `Init()` / `Shutdown()`，版本信息是纯常量，随便什么时候调。

## 实测（`xgcore_demo.exe`）

一个 TU 里同时 include 12 个模块头并交叉调用：

```
[1] version=1.0.0 (1.0.0) build=Nov 24 2026 09:58:23
[2] modules=12: xgdl xglog xgproc xgcrypto xgzip xgfs xgcfg xgthread xgnet xgsecure xgshell xgcrash
[3] parse=1 write=1 read=1 exists=1 name=XGStudio
[4] base64=WEdTdHVkaW8= knownFolder=1 path=C:\Users\XG\AppData\Local
done: OK
```

## 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

```cmake
set(XGCORE_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cpploclib/modCore)
target_link_libraries(app PRIVATE XGCORE::xgcore)
```

安装后：`find_package(xgcore 1.0 REQUIRED CONFIG)` + `XGCORE::xgcore`。

> 单独 configure `modCore` 目录时没有其他模块目标，`Modules()` 是空列表、聚合头只包含自己——这是预期行为。
