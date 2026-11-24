// Copyright 2026 XGstudio
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// XGCORE —— 聚合头 + 版本信息。
//
// 这里不做任何业务，只做两件事：
//   1. 一次性把已构建模块的公开头收进来（用 __has_include 判断，缺哪个就跳过）；
//   2. 提供版本号与编译期信息，方便上层打日志、做兼容判断。
//
// 只想用一两个模块时，直接 include 对应模块头（<xgdl/xgdl.h> 之类），不要引这个聚合头。

#define XGCORE_VERSION_MAJOR 1
#define XGCORE_VERSION_MINOR 0
#define XGCORE_VERSION_PATCH 0
#define XGCORE_VERSION_STRING "1.0.0"

#if __has_include(<xgdl/xgdl.h>)
#include <xgdl/xgdl.h>
#define XGCORE_HAS_XGDL 1
#endif

#if __has_include(<xglog/xglog.h>)
#include <xglog/xglog.h>
#define XGCORE_HAS_XGLOG 1
#endif

#if __has_include(<xgproc/xgproc.h>)
#include <xgproc/xgproc.h>
#define XGCORE_HAS_XGPROC 1
#endif

#if __has_include(<xgcrypto/xgcrypto.h>)
#include <xgcrypto/xgcrypto.h>
#define XGCORE_HAS_XGCRYPTO 1
#endif

#if __has_include(<xgzip/xgzip.h>)
#include <xgzip/xgzip.h>
#define XGCORE_HAS_XGZIP 1
#endif

#if __has_include(<xgfs/xgfs.h>)
#include <xgfs/xgfs.h>
#define XGCORE_HAS_XGFS 1
#endif

#if __has_include(<xgcfg/xgcfg.h>)
#include <xgcfg/xgcfg.h>
#define XGCORE_HAS_XGCFG 1
#endif

#if __has_include(<xgthread/xgthread.h>)
#include <xgthread/xgthread.h>
#define XGCORE_HAS_XGTHREAD 1
#endif

#if __has_include(<xgnet/xgnet.h>)
#include <xgnet/xgnet.h>
#define XGCORE_HAS_XGNET 1
#endif

#if __has_include(<xgsecure/xgsecure.h>)
#include <xgsecure/xgsecure.h>
#define XGCORE_HAS_XGSECURE 1
#endif

#if __has_include(<xgshell/xgshell.h>)
#include <xgshell/xgshell.h>
#define XGCORE_HAS_XGSHELL 1
#endif

#if __has_include(<xgcrash/xgcrash.h>)
#include <xgcrash/xgcrash.h>
#define XGCORE_HAS_XGCRASH 1
#endif

#include <string>
#include <vector>

namespace xgcore {

struct Version {
    int major = XGCORE_VERSION_MAJOR;
    int minor = XGCORE_VERSION_MINOR;
    int patch = XGCORE_VERSION_PATCH;
};

/** 运行期版本（"1.0.0"）。 */
const char* VersionString();

Version GetVersion();

/** 库本身的编译时间，形如 "Nov 24 2026 09:31:52"。 */
const char* BuildTimestamp();

/** 当前构建里实际编进来的模块名（按字母序，如 "xgcfg"、"xgdl"）。 */
std::vector<std::string> Modules();

} // namespace xgcore
