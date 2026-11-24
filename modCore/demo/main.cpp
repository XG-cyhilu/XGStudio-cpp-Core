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

// 聚合头自检：一个 TU 里同时 include 所有模块，顺手跑几个跨模块调用。

#include <xgcore/xgcore.h>

#include <cstdio>
#include <string>

int main() {
    // 1) 版本
    xgcore::Version ver = xgcore::GetVersion();
    printf("[1] version=%s (%d.%d.%d) build=%s\n", xgcore::VersionString(), ver.major, ver.minor, ver.patch,
           xgcore::BuildTimestamp());

    // 2) 模列表
    auto mods = xgcore::Modules();
    printf("[2] modules=%zu:", mods.size());
    for (const auto& m : mods) printf(" %s", m.c_str());
    printf("\n");

    // 3) 跨模块：XGCFG 解析 → XGFS 落盘 → 读回
    xgcfg::Value v;
    std::string err;
    bool parsed = xgcfg::Parse("{\"app\":{\"name\":\"XGStudio\",\"v\":1}}", v, &err);
    std::wstring path = L"xgcore_demo.json";
    bool wrote = parsed && xgfs::WriteFileAtomic(path, xgcfg::Dump(v, true), &err);
    std::string back;
    bool read = wrote && xgfs::ReadFile(path, back, &err);
    bool exists = xgfs::Exists(path);
    printf("[3] parse=%d write=%d read=%d exists=%d name=%s\n", (int)parsed, (int)wrote, (int)read,
           (int)exists, xgcfg::Parse(back, v, &err) ? v["app"]["name"].AsString("?").c_str() : "?");

    // 4) 跨模块：XGSECURE base64 + XGSHELL 已知文件夹
    std::string b64 = xgsecure::Base64Encode("XGStudio", 8);
    std::wstring appdata = xgshell::KnownFolder(xgshell::Folder::LocalAppData);
    bool kf = !appdata.empty();
    printf("[4] base64=%s knownFolder=%d path=%ls\n", b64.c_str(), (int)kf, appdata.c_str());

    bool ok = parsed && wrote && read && exists && !b64.empty();
    printf("%s\n", ok ? "done: OK" : "done: FAIL");

    _wremove(path.c_str());
    return ok ? 0 : 1;
}
