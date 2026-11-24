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

// The umbrella header must compile in a single TU; that alone is the main assertion here.

#include <xgcore/xgcore.h>

#include "../test_util.h"

int main() {
    XGT_CHECK(std::string(xgcore::VersionString()) == XGCORE_VERSION_STRING);

    xgcore::Version v = xgcore::GetVersion();
    XGT_CHECK(v.major == 1 && v.minor == 0 && v.patch == 0);

    XGT_CHECK(std::string(xgcore::BuildTimestamp()).size() > 10);

    auto mods = xgcore::Modules();
    XGT_CHECK(mods.size() >= 10);
    bool hasCfg = false, hasDl = false;
    for (const auto& m : mods) {
        if (m == "xgcfg") hasCfg = true;
        if (m == "xgdl") hasDl = true;
    }
    XGT_CHECK(hasCfg);
    // xgdl may legitimately be absent (CI builds without the static libcurl)
    if (!hasDl) printf("note: xgdl not in this build\n");

    // cross-module smoke: parse -> atomic write -> read back -> hash
    std::string err;
    xgcfg::Value cfg;
    XGT_CHECK(xgcfg::Parse(R"({"app":"XGStudio"})", cfg, &err));
    std::wstring dir = xgt::TempDir(L"core");
    std::wstring path = dir + L"\\core.json";
    std::string text = xgcfg::Dump(cfg, true);
    XGT_CHECK(xgfs::WriteFileAtomic(path, text, &err));
    std::string back;
    XGT_CHECK(xgfs::ReadFile(path, back, &err));
    XGT_CHECK(back == text);
    XGT_CHECK(xgcrypto::HashHex(xgcrypto::Alg::Sha256, back.data(), back.size()).size() == 64);

    xgt::DropDir(dir);
    XGT_FINISH("core");
}
