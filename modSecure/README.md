# XGSECURE

编解码与密钥材料（Windows / C++17 / 静态库 / Apache-2.0），只用 CNG（`bcrypt.lib`），无第三方依赖。

XGCRYPTO 负责"算摘要"，这里负责"跟密钥打交道"的那部分：Base64、密码学随机、HMAC-SHA256、AES-256-GCM、PBKDF2。全部是把 CNG 的调用顺序固定下来——顺序写错（比如 GCM 忘了设 chaining mode、或者 iv 复用）后果都很安静。

```cpp
#include <xgsecure/xgsecure.h>

// PKCE（OAuth 授权码流程）
std::string verifier = xgsecure::RandomBase64Url(32);
std::vector<uint8_t> digest = xgcrypto::Hash(xgcrypto::Alg::Sha256, verifier.data(), verifier.size());
std::string challenge = xgsecure::Base64UrlEncode(digest);

// token 落盘加密
std::vector<uint8_t> key = xgsecure::KeyFromPassphrase("user passphrase", salt);
std::string sealed;
xgsecure::SealText(key, refreshToken, sealed, "account/msa");
```

## Base64

| 接口 | 说明 |
| --- | --- |
| `Base64Encode(ptr, len)` / `(vector)` | 标准字母表，带 `=` 填充 |
| `Base64UrlEncode(...)` | `+/` → `-_`，去掉填充（RFC 4648 §5 / JWT / PKCE） |
| `Base64Decode(text, out)` | 两种字母表都吃，忽略空白与 `=`；非法字符或 `len % 4 == 1` 返回 `false` |

## 随机

| 接口 | 说明 |
| --- | --- |
| `RandomBytes(ptr, len)` / `RandomBytes(len)` | `BCryptGenRandom` + 系统首选 RNG；失败返回 `false` / 空 |
| `RandomHex(bytes)` | 小写十六进制，长度 = `bytes * 2` |
| `RandomBase64Url(bytes)` | 无填充，可安全做文件名/URL 片段 |

## HMAC-SHA256

| 接口 | 说明 |
| --- | --- |
| `HmacSha256(key, keyLen, data, len)` | `BCRYPT_ALG_HANDLE_HMAC_FLAG` + `BCryptCreateHash` 带 key |
| `HmacSha256Hex(...)` | 十六进制结果 |
| `HmacSha256Verify(key, data, hex)` | 定长比较（自己写 `==` 会按首个不同字节提前返回） |

## AES-256-GCM

密封后的字节布局（平坦、带版本号，便于以后换算法）：

```
[0]      version = 1
[1..12]  iv（每次 Seal 随机生成 12 字节）
[13..28] tag（16 字节）
[29..]   ciphertext（与明文等长）
```

| 接口 | 说明 |
| --- | --- |
| `Seal(key32, plain, len, out, aad, aadLen)` | 每次随机 iv；AAD 参与认证（可把 `account/msa` 这类上下文绑进来） |
| `Unseal(...)` | 篡改、错 key、AAD 不匹配都返回 `false`（GCM 认证失败是同一个出口） |
| `SealText` / `UnsealText` | 外面再套一层 Base64，方便写进 JSON/配置 |

**不要自己传 iv**：GCM 下 iv 复用会让攻击者恢复明文并伪造 tag，所以 `Seal` 内部固定随机生成，接口也不暴露。

## PBKDF2

`KeyFromPassphrase(pass, salt, iterations = 210000)` → 32 字节密钥（`BCryptDeriveKeyPBKDF2` + HMAC-SHA256）。默认 210000 是 OWASP 2023 对 PBKDF2-SHA256 的建议下限；加盐请用 `RandomBytes(16)` 并存进配置。

## 实测（`xgsecure_demo.exe`）

| 项 | 结果 |
| --- | --- |
| Base64 `"hello world"` | `aGVsbG8gd29ybGQ=`，解码回原文 |
| Base64Url `0xFB 0xFF` | `-_8`（无填充，`+`/`/` 已替换） |
| 两次 16 字节随机 | 不相等；hex 长度 16 |
| HMAC-SHA256（RFC 4231 Case 2） | `f7bc83f4…2d1a3cd8`，与向量一致；错 key verify=0 |
| AES-GCM 往返 | 68 字符密封串（1+12+16+18 再 Base64），解密回原文 |
| 错 key / 密文改动 / AAD 不匹配 | 全部 `Accepted=0` |
| PBKDF2（RFC 7914 向量，c=1） | `120fb6cf…70be17b` 一致 |

## 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

```cmake
set(XGSECURE_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cpploclib/modSecure)
target_link_libraries(app PRIVATE XGSECURE::xgsecure)   # bcrypt 会自动带上
```

安装后：`find_package(xgsecure 1.0 REQUIRED CONFIG)` + `XGSECURE::xgsecure`。

> 公开头与源码均为 UTF-8 带 BOM（外部 MSVC 工程不加 `/utf-8` 时按 936 解析，中文注释会直接编译失败）。
