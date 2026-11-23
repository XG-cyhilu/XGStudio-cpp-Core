# XGFS

文件与路径工具（Windows / C++17 / 静态库 / Apache-2.0），无第三方依赖。

只收那些"每个项目都重写一遍、每次都可能写错"的部分：**路径规范化、原子写、临时文件、递归遍历、安全删除**。

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
