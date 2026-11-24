# XGCrash

崩溃转储：把未处理异常（SEH）、`std::terminate`、可选的 VEH 三路都汇到 `MiniDumpWriteDump`，
落盘到固定目录并自动清理旧文件。只依赖 `dbghelp`，不需要外部崩溃上报服务。

```cpp
#include <xgcrash/xgcrash.h>

xgcrash::Options opt;                 // dumpDir 空 = %LOCALAPPDATA%\<app>\CrashDumps
opt.maxDumps = 10;                    // 只留最近 10 个
opt.catchVectored = true;             // 想抓 SEH 抓不到的部分就打开
opt.onWritten = [](const std::wstring& p) { /* 下次启动时可以提示"上次崩溃了" */ };
xgcrash::Install(opt);                // main 里最先调用
```

## 能力

| API | 说明 |
|---|---|
| `Install(Options)` | 幂等：重复调用先撤旧再装新。返回 false 只在目录建不出来时 |
| `Shutdown()` | 恢复之前的 SEH 过滤器 / terminate handler（测试用；正常退出不用调） |
| `DefaultDumpDir()` | `%LOCALAPPDATA%\<app>\CrashDumps`，取不到 LocalAppData 时退回 `%TEMP%` |
| `WriteDump(ep, tag, &path)` | 手写一份；`ep` 可为 `nullptr`（普通内存转储） |
| `ListDumps()` | 目录里的 `.dmp`，按文件名（即时间戳）从旧到新 |
| `PruneDumps(max)` | 超出的从最旧删起，返回删除数 |

`Options` 字段：`dumpDir` / `appName` / `maxDumps` / `fullMemory` / `catchTerminate` /
`catchVectored` / `onWritten`。

## 转储类型

默认 `MiniDumpNormal | WithHandleData | WithThreadInfo | WithUnloadedModules`——够看调用栈和
模块列表，一个文件通常几十 KB。`fullMemory = true` 会改成 `MiniDumpWithFullMemory`，
文件大小接近进程工作集，只在需要复现复杂状态时打开。

## 设计取舍

- **不弹窗、不上报**。库只负责"把现场留下来"，提示用户、打包上传、重启进程都属于调用方的产品逻辑。
  写完后通过 `onWritten` 同步回调一次。
- **写 dump 期间禁止重入**。SEH、VEH、terminate 可能同时命中，内部用原子标志保证只写一次，
  否则 dump 代码自己崩会把进程拖进死循环。
- **栈溢出不进 VEH**。`EXCEPTION_STACK_OVERFLOW` 时栈已经不可用，再调用写盘函数会在里面二次崩，
  因此 VEH 里遇到它直接放行，交给 SEH 过滤器（它跑在崩溃处理线程上，栈是好的）。
- **文件名带时间戳 + PID + 序号**。同一秒内多次崩溃也不会互相覆盖，排序即时间顺序。

## 示例

```powershell
xgcrash_demo            # 装处理器 → 手写 3 个 dump → 验证只留 2 个
xgcrash_demo --crash    # 故意空指针，验证真实崩溃能抓到现场
```

实测（Win10 19044，Release）：

| 检查 | 结果 |
|---|---|
| 手写 dump（`MiniDumpNormal` 组合） | 3 个文件，约 59–60 KB |
| `maxDumps=2` 清理 | 写入 3 个后目录里剩 2 个 |
| `--crash` 空指针解引用 | 退出码 `0xC0000005`，落盘 `..._veh.dmp`（59.6 KB），只写一份 |

## 目录

```
modCrash/
├─ include/xgcrash/xgcrash.h   唯一公开头
├─ src/xgcrash.cpp             SEH / terminate / VEH + MiniDumpWriteDump
├─ demo/main.cpp               手写 dump 自检 + --crash 真崩验证
└─ CMakeLists.txt
```

## 许可

Apache License 2.0，见仓库根目录 `LICENSE`。
