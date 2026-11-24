# XGCFG

JSON 值 + 配置文件封装（Windows / C++17 / 静态库 / Apache-2.0 / 无第三方依赖）。

两件事：
1. **够用的 JSON 值树**：`Parse` / `Dump`，解析时容忍注释与尾逗号；
2. **配置文件封装**：点路径访问、带默认值的取值、原子保存、按 mtime 重载。

```cpp
#include <xgcfg/xgcfg.h>

xgcfg::Config cfg;
cfg.Load(L"D:\\game\\config.json");

int width = static_cast<int>(cfg.GetInt("window.width", 1280));   // 缺字段就用默认
bool vsync = cfg.GetBool("window.vsync", true);

cfg.Set("window.width", xgcfg::Value(1600));
cfg.Set("mods[0].id", xgcfg::Value(std::string("fabric-api")));
cfg.Save();                                                       // 原子写，不会留半截文件

if (cfg.Changed()) cfg.Reload();                                   // 用户手改了文件
```

## 为什么不用现成 JSON 库

nlohmann/json 很好，但它意味着：一个 200KB+ 的模板重编译、`-fno-exceptions` 工程要额外配置、错误处理走异常。对"读一个配置文件"这件事，够用的 500 行解析器 + 明确的 `bool` 返回值更省事。取舍已经写在能力边界里。

## JSON 解析的宽严

| 特性 | 解析 | 输出 |
| --- | --- | --- |
| 标准 JSON | 支持 | 标准 JSON |
| `//` 与 `/* */` 注释 | 支持（`allowLoose`，默认开） | 不输出 |
| 尾逗号 | 支持 | 不输出 |
| `\uXXXX` 与代理对 | 支持（转成 UTF-8） | 原样按 UTF-8 输出 |
| 嵌套深度 | 上限 500 层 | — |
| `NaN` / `Inf` | 不接受 | 输出 `null` |

错误信息带字节偏移：`json parse error at byte 6: unexpected character`。

## Value

`Type`：`Null / Bool / Int / Real / String / Array / Object`。数字区分整数与实数，`1` 存 `Int`，`1.0` 与 `1e3` 存 `Real`；输出时实数用最短往返表示（`std::to_chars`）。

| 方法 | 说明 |
| --- | --- |
| `AsBool / AsInt / AsDouble / AsString` | 带默认值的宽松转换（`Int→Real`、`Bool→0/1`、数字→字符串） |
| `Size / At / Push / RemoveAt` | 数组 |
| `Has / Find / operator[] / Remove / Keys` | 对象；**保持插入顺序**，便于手改与 diff |
| `FindPath / SetPath / RemovePath` | 点路径 `a.b[0].c`；`SetPath` 自动建中间对象与数组 |

`operator[]` 不存在会创建；需要"只读、不要副作用"时用 `Find` / `FindPath`。

## Config

| 接口 | 说明 |
| --- | --- |
| `Load / LoadText / Save / SaveAs` | `Save` 走同目录 `.tmp` + `FlushFileBuffers` + `MoveFileEx(REPLACE\|WRITE_THROUGH)` |
| `Path / Root` | 当前文件与根值 |
| `Changed` | 只比 mtime + 大小，不读内容；适合 100~200ms 轮询一次 |
| `Reload` | 重新加载（失败保留旧内容） |
| `Has / Remove / GetBool / GetInt / GetDouble / GetString / Set` | 点路径读写，都有默认值参数 |
| `Dirty` / `SetOnChange` | 自 `Load`/`LoadText`/`Save`/`SaveAs`/`Reload` 之后是否又被改过；每次生效的 `Set`/`Remove` 触发一次回调（`Remove` 传入 `null` 值） |
| `Merge(other, overwriteExisting)` | 对象深合并；数组与标量按 `overwriteExisting` 整体替换；返回变更个数 |

`Config::Load` 要求根是对象（数组根没有"点路径"意义，直接用 `Parse`）。

## Binder：结构体与配置双向同步

字段多了以后手写 `GetInt("ui.width", ...)` 容易拼错、也容易漏存。`Binder` 只记字段地址，值仍留在你自己的结构体里：

```cpp
struct Settings { bool tray = true; int threads = 4; double scale = 1.0; std::string theme = "dark"; } s;

xgcfg::Binder b(cfg, "ui");                 // prefix 可为空，表示根层
b.BindBool("tray", s.tray)
 .BindInt("threads", s.threads)
 .BindDouble("scale", s.scale)
 .BindString("theme", s.theme);

b.Pull();   // 配置 → 结构体：键不存在时**不动**默认值，返回真正变化的字段数
b.Push();   // 结构体 → 配置：只写有差异的键（缺键会新建），同样返回变化数
```

`Pull` 不会把缺省项写成 `null` 或清零，所以"默认值写在结构体初始化里、用户只覆盖他想改的"是稳的。`Binder` 持有字段引用，声明周期不要短于被绑定的变量。

## 实测（`xgcfg_demo.exe`）


| 项 | 结果 |
| --- | --- |
| 带行注释/块注释/尾逗号的输入 | 解析成功：`name/version/ratio/enabled/null/list` 全对 |
| `\u4e2d\u6587 \ud83d\ude00` | 解成 `值 中文 😀`（代理对正确合成） |
| 非法输入 `{"a": }` | `byte 6: unexpected character` |
| `Dump(compact)` | `{"name":"XGStudio","version":3,...,"list":[1,2,3]}` |
| 点路径写 `mods[0].id` / `mods[1].enabled` | 落盘后回读正确（326 字节美化输出） |
| `GetInt("no.such.key", -1)` | 返回 `-1` |
| 手改文件 → `Changed` → `Reload` | `changed=1 reloaded=1`，读到 `edited / 9` |
| `Dump → Parse → Dump` | 紧凑结果完全一致 |
| `Dirty()`（改/存/载） | 载入后 `0` → `Set` 后 `1` → `SaveAs` 后 `0` |
| 变更回调 | `Set×2 + Remove×1` → `hits=3`，最后一次 `ui.scale` 且值为 `null` |
| `Merge(user, false)` | `0` 处变更（同名键全部保留默认） |
| `Merge(user, true)` | `2` 处变更（`ui.theme` 覆盖，`mods` 数组整体替换） |
| `Binder` | `Pull` 拉到 `theme=light / threads=8` 共 `2` 个字段；`Push` 写回 `2` 处 |

## 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

```cmake
set(XGCFG_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cpploclib/modCfg)
target_link_libraries(app PRIVATE XGCFG::xgcfg)
```

安装后：`find_package(xgcfg 1.0 REQUIRED CONFIG)` + `XGCFG::xgcfg`。

> 公开头与源码均为 UTF-8 带 BOM（外部 MSVC 工程不加 `/utf-8` 时按 936 解析，中文注释会直接编译失败）。
