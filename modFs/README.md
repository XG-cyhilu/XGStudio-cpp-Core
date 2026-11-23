# XGFS

文件与路径工具（Windows / C++17 / 静态库 / Apache-2.0），无第三方依赖。

只收那些"每个项目都重写一遍、每次都可能写错"的部分：**路径规范化、长路径、原子写、临时文件、递归遍历、大小统计、目录替换、安全删除**。

```cpp
#include <xgfs/xgfs.h>

std::string err;
xgfs::EnsureDir(L"D:\\game\\config", &err);
xgfs::WriteFileAtomic(L"D:\\game\\config\\options.json", text, &err);   // 要么旧内容，要么新内容
```

## 路径

| 接口 | 说明 |
| --- | --- |
| `Normalize` | 展开成绝对路径、`/`→`\`、折叠 `..`、去掉多余与末尾分隔符（保留 `C:\`） |
| `Join` | 拼接并规范化；右值是绝对路径时直接用右值 |
| `FileName` / `Stem` / `Extension` / `Parent` | 拆路径，`Extension` 含点 |
| `ReplaceExtension` | 换扩展名（`ext` 带不带点都行） |
| `LongPath` | 超过 248 字符时补 `\\?\`（UNC 转 `\\?\UNC\`），短路径原样返回绝对化结果 |

**长路径**：库内部所有 `CreateFile / FindFirstFileEx / MoveFileEx / DeleteFileW / RemoveDirectoryW` 都自动套 `\\?\` 前缀，所以 `EnsureDir` / `WriteFile` / `ListDir` / `ReplaceDirectory` 直接吃超长路径就行。只有把路径交给第三方（libcurl、外部 DLL）时才需要手动 `LongPath()`。前缀只在 ≥248 字符时加——因为 `\\?\` 会关掉 Win32 的路径归一化（`.`、`..`、`/` 全部失效），无脑加反而会让短路径行为变味。

## 读写

| 接口 | 说明 |
| --- | --- |
| `ReadFile(path, std::string&)` | 二进制安全，先 stat 定长再读，1MB 分块 |
| `ReadFile(path, std::vector<uint8_t>&)` | 同上，给字节场景 |
| `WriteFile` | `CREATE_ALWAYS` + 1MB 分块写 |
| `WriteFileAtomic` | 同目录 `.tmp` → `FlushFileBuffers` → `MoveFileEx(REPLACE_EXISTING\|WRITE_THROUGH)` |

`WriteFileAtomic` 的意义：配置文件写一半掉电，普通写会留下半截 JSON，下次启动直接解析失败；原子写保证磁盘上永远是"完整的旧版本"或"完整的新版本"。

## 移动 / 复制 / 删除

`Move`（跨卷也可，`MoveFileEx` + `WRITE_THROUGH`）、`Copy`（可选覆盖）、`Remove`（`recursive` 时先清子项再删自己）。三者都会自动创建目标父目录。

| 接口 | 说明 |
| --- | --- |
| `RemoveWithRetry(path, recursive, retries=5, delayMs=120)` | 删除失败先清只读属性再重试。被杀软/索引器/刚写完的文件短暂占用是 Windows 常态，一次 `DeleteFileW` 失败就报错让上层很痛苦 |
| `ReplaceDirectory(src, dst)` | 原子式目录替换：`dst→dst.old` → `src→dst` → 删 `dst.old`；第二步失败会把 `dst.old` 移回去。解压/更新场景用它，避免"旧的删了新的没进来" |

`ReplaceDirectory` 的 `src` 与 `dst` 必须在同一个卷上（rename 语义，跨卷会直接失败），要跨卷就先 `Copy` 到目标卷的临时目录再替换。

## 查询与统计

| 接口 | 说明 |
| --- | --- |
| `DirSize(dir, bytes)` | 递归累加普通文件大小（不跟进 reparse point） |
| `DiskSpace(path, free, total)` | 所在卷的可用/总容量，`GetDiskFreeSpaceExW` 封装。下载前判断空间够不够 |
| `CreateTempDir(dirHint, path)` | 唯一目录名并立即创建 |

## 遍历

```cpp
xgfs::ListOptions opt;
opt.recursive = true;
opt.pattern = L"*.json";      // 支持 * ?，大小写不敏感
std::vector<xgfs::DirEntry> out;
xgfs::ListDir(L"D:\\game", out, opt, &err);
```

`DirEntry` 给 `name / path（绝对） / size / mtimeMs / isDir`。`FindFirstFileExW` + `FIND_FIRST_EX_LARGE_FETCH`，不跟进目录联接（避免 junction 造成的死循环）。

## 实测（`xgfs_demo.exe`）

| 项 | 结果 |
| --- | --- |
| `..\xgfs\a//b\\c\` 规范化 | 折叠为绝对路径，分隔符统一，末尾 `\` 去掉 |
| `Join(root, "sub/../file.txt")` | `D:\root\file.txt` |
| 拆名 | `mod.jar` / `mod` / `.jar` / `D:\a\b\mod.json` |
| 普通写 + 原子写 + 读回 | 12 / 7 字节，内容一致 |
| 递归遍历 9 项 + `*.json` 过滤 | 数量与名字正确 |
| 复制 → 移动 → 删除 | 全成功，删除后 `Exists` 为 0 |
| 临时文件 | `xgtXXXX.tmp` 生成并在指定目录 |
| 临时目录 | 唯一目录名，`IsDir=1` |
| `DirSize` | 34 字节（12 + 7 + 15，与写入量一致） |
| `DiskSpace` | 74.3 GB 可用 / 280.0 GB 总容量 |
| 324 字符长路径写 + 读 | 写入 9 字节并读回，`\\?\` 前缀生效 |
| `ReplaceDirectory` | 目标目录内容换成 `v2`，`target_b.old` 无残留 |
| `Remove(recursive)` | 整个测试目录清空 |

## 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

```cmake
set(XGFS_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cpploclib/modFs)
target_link_libraries(app PRIVATE XGFS::xgfs)
```

安装后：`find_package(xgfs 1.0 REQUIRED CONFIG)` + `XGFS::xgfs`。

> 公开头与源码均为 UTF-8 带 BOM（外部 MSVC 工程不加 `/utf-8` 时按 936 解析，中文注释会直接编译失败）。
