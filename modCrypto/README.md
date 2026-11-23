# XGCRYPTO

摘要计算（Windows / C++17 / 静态库 / Apache-2.0），基于系统 CNG（`bcrypt.dll`）。

只做"算哈希"：MD5 / SHA1 / SHA256，字节流或文件，分块或一次性，结果给字节或十六进制。

```cpp
#include <xgcrypto/xgcrypto.h>

auto sha256 = xgcrypto::HashHex(xgcrypto::Alg::Sha256, data, len);
auto fileHash = xgcrypto::HashFileHex(xgcrypto::Alg::Sha256, L"setup.zip");
```

## 为什么不自己写算法实现

MD5/SHA 自己写的问题不在于写不出来，而在于**没人会去测边界**：消息长度 55/56/64 字节的补位边界、SHA1 的 80 轮溢出、SHA256 的 32 位加法回绕。CNG 是系统自带、被 fuzzing 过的实现，调用成本也就一次 `BCryptOpenAlgorithmProvider`。

代价是 Windows only，和 XGDL / XGPROC / XGZIP 一致，不是额外限制。

## API

| 接口 | 说明 |
| --- | --- |
| `Digest(Alg)` | 增量喂数据：`Update(data, len)` 多次 → `Final(bytes)` |
| `Hash(Alg, data, len)` | 一次性，返回字节 |
| `HashHex(Alg, data, len)` | 一次性，返回小写十六进制 |
| `HashFile(Alg, path)` / `HashFileHex` | 读文件（256KB 块，不整文件进内存） |
| `DigestSize(Alg)` | 16 / 20 / 32 |
| `ToHex` / `FromHex` | 字节 ↔ 十六进制；`FromHex` 遇非法字符或奇数长度返回空 |
| `Equals(a, b)` | 定长比较，不按前缀提前退出 |

`Final` 可以重复调用（第二次直接给缓存），`Final` 之后再 `Update` 返回 false——不搞静默重算。

```cpp
xgproc::Process p;   // 举例：把外部程序的输出直接喂给哈希
xgproc::Line ln;
xgproc::Digest d(xgcrypto::Alg::Sha256);
while (p.Poll(ln, 100)) d.Update(ln.text.data(), ln.text.size());
std::vector<uint8_t> h;
d.Final(h);
```

## 与校验场景的配合

下载/解压后的校验建议用 `Equals` 比较字节，而不是比较十六进制字符串：

```cpp
bool ok = xgcrypto::Equals(xgcrypto::HashFile(xgcrypto::Alg::Sha1, path),
                           xgcrypto::FromHex(expectedHex));
```

`FromHex` 接受大小写混合输入，所以从网页/JSON 里抄来的大写哈希也能直接用。

## 实测（`xgcrypto_demo.exe`）

| 项 | 结果 |
| --- | --- |
| 空串 SHA256 | `e3b0c44298fc1c14...b855`（标准向量） |
| `abc` SHA256 / SHA1 / MD5 | 三组标准向量全部一致 |
| 3MB 数据分块喂入 vs 一次性 | 结果相同 |
| 写文件再 `HashFile` | 与内存结果相同 |
| hex 往返 | 32 字节往返一致；`"xyz"` 判非法返回空 |

依赖只有 `bcrypt`（系统库），没有第三方。

## 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

作为子目录：

```cmake
set(XGCRYPTO_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cpploclib/modCrypto)
target_link_libraries(app PRIVATE XGCRYPTO::xgcrypto)
```

安装后消费：

```cmake
find_package(xgcrypto 1.0 REQUIRED CONFIG)
target_link_libraries(app PRIVATE XGCRYPTO::xgcrypto)
```

> 公开头与源码均为 UTF-8 带 BOM（外部 MSVC 工程不加 `/utf-8` 时按 936 解析，中文注释会直接编译失败）。
