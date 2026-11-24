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

#include <xgcore/xgcore.h>

#if __has_include("xgcore_modules.h")
#include "xgcore_modules.h"   // 构建期生成：本次实际编进来的模块名
#endif

#ifndef XGCORE_MODULE_LIST
#define XGCORE_MODULE_LIST
#endif

namespace xgcore {

namespace {

const char* const kModules[] = {
    XGCORE_MODULE_LIST
    nullptr};

} // namespace

const char* VersionString() { return XGCORE_VERSION_STRING; }

Version GetVersion() { return Version(); }

const char* BuildTimestamp() { return __DATE__ " " __TIME__; }

std::vector<std::string> Modules() {
    std::vector<std::string> out;
    for (const char* const* it = kModules; *it; ++it) out.emplace_back(*it);
    return out;
}

} // namespace xgcore
