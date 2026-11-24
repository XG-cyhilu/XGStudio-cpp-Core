# XGZIP

zip 读取、解压与写入（Windows / C++17 / 静态库 / Apache-2.0 / **无第三方依赖**）。

deflate 编解码都是库里自带的实现（解压覆盖 store / fixed / dynamic 三种块，压缩输出固定 Huffman），
不链 zlib、不链 miniz。做整合包、更新包、资源包时不需要为了一个 deflate 引入整套依赖。

```cpp
#include <xgzip/xgzip.h>

std::vector<xgzip::Entry> es;
std::string err;
xgzip::List(L"pack.zip", es, err);      // 只看目录，不解压

xgzip::ExtractOptions opt;
opt.filter = L"assets\\*";              // 只解一部分；空 = 全部
xgzip::Result r;
xgzip::Stats st;                        // 进度：另一线程随时读
xgzip::Extract(L"pack.zip", L"D:\\target", opt, r, &st);
```

写包：

```cpp
xgzip::WriteOptions wopt;
wopt.method = xgzip::WriteMethod::Deflate;   // 或 Store
xgzip::Writer w(L"out.zip", wopt);
if (!w.Begin(err)) return;
w.AddDirectory(L"assets");
w.AddFile(L"assets\\pack.mcmeta", L"D:\\src\\pack.mcmeta", err);   // 流式（Store）/ 内存（Deflate）
w.AddMemory(L"version.json", json.data(), json.size(), err);
w.Commit(err);                                // 写中央目录，<zip>.tmp 一步改名到正式文件
```

## 能力边界

- 支持 `store`（method 0）与 `deflate`（method 8），解压支持 ZIP64（EOCD / locator / extra 0x0001），
  不支持分卷、不支持加密条目（`en.encrypted` 会标出来，解压时直接报错）。
- **写入只产出经典 zip**（单条目 / 总包 < 4GB）。超过限制会返回 `zip64 required` 错误而不是静默截断。
- **写入先落 `<zip>.tmp`**，`Commit` 成功才 `MoveFileEx(REPLACE_EXISTING)`，失败或 Abort 不留半个包。
- 路径先规范化再落盘：`..`、绝对路径、带盘符的条目一律拒绝并计入失败，不会写出目标目录；
  写入侧同样拒绝这些名字。
- 中文名：`0x0800` 位为 UTF-8 时直接按 UTF-8 解，否则按当前 ANSI 页（资源管理器压出来的包就是后者）。
  写入时名字含非 ASCII 字符会自动置该位。

## API

| 接口 | 说明 |
| --- | --- |
| `List(path, entries, err)` | 读中央目录，返回 `Entry`（名/大小/压缩后大小/CRC/method/是否目录/是否加密） |
| `Extract(path, dest, opt, res, stats)` | 解整包或按 `filter` 解一部分 |
| `ReadEntry(path, name, bytes, err)` | 单个条目解到内存（配置、清单这类小文件用） |
| `Crc32(seed, data, len)` | 标准 zip CRC-32，`seed` 传 0 |
| `Writer(zip, wopt)` | 顺序写新包；`Begin / AddFile / AddMemory / AddDirectory / Commit / Abort` |
| `Writer::BytesWritten / EntriesWritten` | 写完后的统计（进度、日志用） |

`WriteOptions`：

| 字段 | 默认 | 说明 |
| --- | --- | --- |
| `method` | `Store` | `Store` 流式复制（内存恒定）；`Deflate` 先压后写（单条大小受内存限制） |
| `effort` | 6 | Deflate 的 LZ77 搜索强度 1..9；越高越慢、压得越小 |
| `preserveMtime` | true | 取磁盘文件时间写进 DOS 时间字段 |
| `utf8Names` | true | 非 ASCII 名字自动打 UTF-8 标志位 |

`ExtractOptions`：

| 字段 | 默认 | 说明 |
| --- | --- | --- |
| `filter` | 空 | 空 = 全部；支持 `*` `?` 通配（大小写不敏感），或以 `\` 结尾时按目录前缀匹配 |
| `overwrite` | true | false = 目标已存在就跳过（计入 `skipped`） |
| `keepPaths` | true | false = 全部摊平到目标目录（重名会互相覆盖，慎用） |

`Result` 给出 `files / dirs / skipped / bytes / elapsedMs / failed[]`；任何条目失败 `ok=false`，`error` 是第一个原因。每个文件解完都会校验 **size 与 CRC32**，不符就删掉该文件并记为失败——半截文件不会留在磁盘上。

`Stats` 是 `atomic` 计数器（`bytesDone / bytesTotal / filesDone / filesTotal`），传给 `Extract` 后由别的线程读即可，不需要回调。

```cpp
// 进度条写法
xgzip::Stats st;
std::thread ui([&] {
    while (running) {
        uint64_t total = st.bytesTotal.load();
        printf("%.1f%%\n", total ? 100.0 * st.bytesDone.load() / total : 0.0);
        Sleep(100);
    }
});
xgzip::Extract(zip, dest, opt, r, &st);
```

## 实现要点

- **中央目录解析**：从尾部倒查 EOCD（注释最长 65535，所以最多看 65557 字节）；`0xFFFF/0xFFFFFFFF` 标记出现时读 ZIP64 EOCD 与 locator，条目级 64 位大小/偏移走 extra 0x0001。
- **数据起点**：本地头的 name/extra 长度可能与中央目录不同（ST 包、补丁包常见），所以单独读本地头算偏移，不拿中央目录的值硬套。
- **inflate**：逐位规范 Huffman 解码（表小、代码短、不容易写错），输出侧是"32KB 环形窗口 + 32KB 写块缓冲"：窗口供 `< 32KB` 的回填距离使用，写块保证 `fwrite` 不是一字节一次。
- **CRC 边写边算**：sink 在写盘的同时累计 CRC，解完即校验，不需要回读文件。
- **deflate 编码器**：单块固定 Huffman（BFINAL=1 / BTYPE=01）+ 32KB 窗口 LZ77，哈希链深度随
  `effort` 变化。压完发现比原数据大就自动退回 store，不会产生"越压越大"的条目。
- **Store 写入两遍读**：第一遍只算 CRC，第二遍边读边写，所以不会把整个文件读进内存；
  Deflate 写入需要先压缩，按条目大小占用内存。

## 实测（`xgzip_demo.exe list|extract|write`）

| 包 | 内容 | 结果 |
| --- | --- | --- |
| .NET `Compress-Archive`（Optimal） | 205 条目：200 小文本 + 5MB 随机 + 1MB 全零 + 中文名 + 深层目录 | 205/205 成功，CRC 全过，逐文件 SHA256 与源一致 |
| 同上（NoCompression） | store 分支 | 205/205 成功，结果一致 |
| Python `zipfile`（level 1） | 固定 Huffman 短串、空文件、UTF-8 中文名、2 万字节噪声 | 6/6 成功，`sub\deep\中文.txt` 正确落盘 |
| 44MB 文本 + 8MB 随机 | deflate 大文件 | 52.5MB / 456ms（约 115MB/s），SHA256 一致 |
| 恶意路径包（`../evil.txt`、`..\evil2.txt`、`C:/abs.txt`） | 构造的穿越条目 | 3 条全部拒绝，只有 `ok.txt` 落盘，目标目录外无文件 |
| 自写包（Store） | 目录条目 + 56.8KB 文本 + 64KB 周期数据 + 300KB 随机磁盘文件 | 4 条目 429984 字节；自家 `List`/`ReadEntry` 逐条比对一致；.NET `ZipFile` 可读，长度/压缩长度一致 |
| 自写包（Deflate, effort=6） | 同上 | 310102 字节；文本 56800→562，周期数据 65536→1892，随机文件自动退回 store（307200→307200）；.NET 可读，自家解压 2/2 成功 |

```powershell
xgzip_demo list pack.zip
xgzip_demo extract pack.zip D:\out "assets\*"
xgzip_demo write out.zip deflate
```

## 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

作为子目录：

```cmake
set(XGZIP_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cpploclib/modZip)
target_link_libraries(app PRIVATE XGZIP::xgzip)
```

安装后消费：

```cmake
find_package(xgzip 1.0 REQUIRED CONFIG)
target_link_libraries(app PRIVATE XGZIP::xgzip)
```

> 公开头与源码均为 UTF-8 带 BOM（外部 MSVC 工程不加 `/utf-8` 时按 936 解析，中文注释会直接编译失败）。
