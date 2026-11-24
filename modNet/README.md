# XGNET

同步 HTTP 客户端（Windows / C++17 / 静态库 / Apache-2.0），后端是静态 libcurl（Schannel）。

```cpp
#include <xgnet/xgnet.h>

xgnet::Client client;                    // 复用一个 easy handle：连接与 TLS 会话都会复用
xgnet::Response res;
if (client.Get("https://registry.npmmirror.com/typescript/latest", res) && res.IsSuccess()) {
    std::string ct;
    if (res.Header("Content-Type", ct)) { /* ... */ }
    // res.body 就是 JSON 文本，交给 XGCFG::Parse 即可
}
```

## 定位

- **同步**：调用线程阻塞到响应返回。库内不另起线程——要后台执行就配 `XGTHREAD::Pool`，这两块是设计成一对的。
- **小负载**：拿 JSON、查版本号、调 API、下载一个几 KB 的清单。大文件请走 XGDL（它有分块 Range、SHA1 校验、背压和重试）。
- **连接复用**：`Client` 内部一个 `CURL*`，`easy_reset` 于每次请求但保留连接池与 TLS 会话；第二次同域请求通常从 200ms 降到 20ms 量级。

## API

| 接口 | 说明 |
| --- | --- |
| `Client::Perform(req, res) → bool` | 同步执行；返回 `false` 时看 `res.error`（HTTP 4xx/5xx 仍返回 `true`） |
| `Client::Get(url, res, timeoutMs)` | GET 快捷方式 |
| `Client::PostJson(url, json, res, timeoutMs)` | POST + `Content-Type: application/json` |
| `xgnet::Get / PostJson` | 一次性便利函数（内部临时建 Client，不复用连接） |

### Request

| 字段 | 默认 | 说明 |
| --- | --- | --- |
| `url` | — | 只接受 `http://` / `https://`，其余直接报错 |
| `method` | `GET` | `GET/POST/PUT/PATCH/DELETE/HEAD`（大写不敏感） |
| `headers` | 空 | `{"Accept","application/json"}` 这种键值对 |
| `body` | 空 | POST/PUT/PATCH/DELETE 的负载 |
| `connectTimeoutMs` | 10000 | 连接阶段 |
| `timeoutMs` | 30000 | 整次请求；`0` = 不限 |
| `followRedirects` / `maxRedirects` | true / 5 | 3xx 跟随 |
| `verifyPeer` | true | 关掉等于不校验证书，只给调试用 |
| `proxy` | 空 | 例如 `http://127.0.0.1:7890`；空且 `useSystemProxy` 时读系统设置 |
| `useSystemProxy` | true | 读 HKCU Internet 设置里的 ProxyEnable / ProxyServer |
| `acceptGzip` | true | 发 `Accept-Encoding: gzip`，响应由本库 gunzip（CRC + ISIZE 校验） |
| `cookieFile` | 空 | 该请求单独使用的 cookie jar 文件；空则用 `Client::SetCookieFile` 设的 |
| `userAgent` | `XGNet/1.0` | |

### Response

`status / body / headers / effectiveUrl / totalSec / received / decompressed / error`，外加
`TransportOk()`、`IsSuccess()`、`Header(name, out)`（大小写不敏感）。

**状态码语义**：能连上就是 `true`，`status=404` 也是 `true`；连不上（DNS、TLS、超时）返回 `false` 且 `status=0`、`error` 是 curl 的错误描述。这样调用方可以分别处理"服务器拒绝"和"网络没通"。

### 工具函数

| 接口 | 说明 |
| --- | --- |
| `SystemProxy(&pac)` | 读 `HKCU\...\Internet Settings`：`ProxyEnable=1` 时返回 `ProxyServer`（按协议列出时优先 https，其次 http）；`pac` 回填 AutoConfigURL（本库不执行 PAC，只报告） |
| `UrlEncode / UrlDecode` | 百分号编解码；保留 `A-Za-z0-9-_.~`，空格编成 `%20`（不是 `+`） |
| `QueryEncode(pairs)` | 组装 `a=1&b=x%20y` |
| `QueryDecode(q)` | 解析成有序 pair；同名参数保留多份；`'+'` 当空格（表单口径） |
| `Client::SetCookieFile / DumpCookies / ClearCookies` | cookie jar 读写与导出（Netscape 文本格式） |

### cookie jar

```cpp
xgnet::Client c;
c.SetCookieFile("cookies.txt");            // CURLOPT_COOKIEFILE + COOKIEJAR
c.Get("https://site/login", res);          // 服务端 Set-Cookie 会被记住并落盘
for (auto& line : c.DumpCookies()) { ... } // domain\tflag\tpath\tsecure\texpiry\tname\tvalue
c.ClearCookies();
```

cookie 引擎是内存表 + 文件落盘：一个 `Client` 内跨请求共享，进程重启后由文件恢复。

## TLS 与压缩

- 静态 libcurl 走 Schannel（不依赖 OpenSSL）。Schannel 的**严格证书吊销检查**在部分网络环境下会把正常证书判死，报 `SSL connect error`。库里设了 `CURLSSLOPT_REVOKE_BEST_EFFORT`：**只降级吊销检查，不关证书校验**。
- 这份静态 libcurl 没链 zlib，自己不会解压。所以 `acceptGzip`（默认开）时由库发 `Accept-Encoding: gzip`，
  收到 `Content-Encoding: gzip` 后用**内置 gunzip**（逐位规范 Huffman + 32KB 窗口，校验 CRC32 与 ISIZE）
  解出明文，`Response::decompressed` 标记是否解过。`br` / `zstd` 不声明也就不可能收到。
- cookie 引擎在 `easy_reset` 后会保留（curl 的 cookie 表是句柄级的），这也是 `Client` 复用同一 handle 的原因之一。

## 实测（`xgnet_demo.exe`）

| 项 | 结果 |
| --- | --- |
| HTTPS GET `registry.npmmirror.com/typescript/latest` | `200`、7817 字节、`Content-Type: application/json; charset=utf-8` |
| 同 Client 第二次请求 | `200`，`0.016s` vs 首次 `0.32s`（连接+TLS 复用明显） |
| 404 | 传输成功、`status=404` |
| POST JSON 到 `postman-echo.com/post` | `200`，body 里回显 `"probe":true`（确认 body 与 Content-Type 真的发出去了） |
| `10.255.255.1` + 600ms 连接超时 | `ok=0 status=0 err=Timeout was reached` |
| 非 http(s) URL | `ok=0 err=only http/https urls are supported` |
| URL / query 编解码 | 中文与 `& = / 空格` 往返一致；`q=hello%20world&lang=…&empty=&q=second` 解析出 5 对（含重复键） |
| gzip 响应（`registry.npmmirror.com/react`） | `200`、`Content-Encoding: gzip`、`decompressed=1`、body 6.92MB 明文 JSON（压缩态约 1.3MB） |
| cookie jar（`httpbin.org/cookies/set?probe=xgnet`） | `200`、`jar=1`、跟随跳转后回显 `probe=xgnet` |

> demo 里踩过一个坑：`printf("%d %s", Perform(...), res.error.c_str())` 这种把"有副作用的调用"和"它的结果"写进同一组参数的写法，参数求值顺序未定义，MSVC 下会先读出未初始化的字符串。已改成先调用再打印。

## 构建

需要静态 libcurl（Schannel 后端，不需要 OpenSSL/zlib）。CMake 会依次探测：

1. `-DXGNET_CURL_DIR=<prefix>`（含 `include/curl/curl.h` 与 `lib/libcurl.lib`）
2. `<repo>/.deps/curl-install`
3. `<repo>/modDownload/.deps/curl-install`（XGDL 自己编出来的那份）
4. 都没有则 `find_package(CURL REQUIRED)`

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

```cmake
set(XGNET_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cpploclib/modNet)
target_link_libraries(app PRIVATE XGNET::xgnet)
```

安装：

```powershell
cmake --install build --prefix C:\libs\xgnet
```

`cmake --install` 会把**静态 libcurl 和 curl 头文件一起装进前缀**，`xgnetConfig.cmake` 在 `CURL::libcurl` 不存在时自动建一个指向前缀内的 imported target。所以消费方只要 `-DCMAKE_PREFIX_PATH=C:\libs\xgnet` + `find_package(xgnet 1.0 REQUIRED CONFIG)`，**不需要系统里有 curl，也不需要 vcpkg**。

> 公开头与源码均为 UTF-8 带 BOM（外部 MSVC 工程不加 `/utf-8` 时按 936 解析，中文注释会直接编译失败）。
