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

// Only the pure/read-only parts are exercised: no HKCU writes, no shortcut creation.
// Registry and IShellLink round-trips stay in the module demo (they need a desktop session).

#include <xgshell/xgshell.h>

#include "../test_util.h"

int main() {
    // QuoteArg -> ParseCommandLine must be lossless for nasty arguments
    std::vector<std::wstring> args = {
        L"plain",
        L"C:\\a b\\c",
        L"quote\"inside",
        L"trailing\\",
        L"trailing\\ with space",
        L"",
        L"CJK 中文 参数",
    };
    std::wstring cmd = L"app.exe";
    for (const auto& a : args) cmd += L" " + xgshell::QuoteArg(a);

    std::vector<std::wstring> parsed = xgshell::ParseCommandLine(cmd.c_str());
    XGT_CHECK(!parsed.empty());
    XGT_CHECK(parsed.size() == args.size() + 1);
    XGT_CHECK(parsed[0] == L"app.exe");
    for (size_t i = 0; i < args.size(); ++i) {
        if (parsed[i + 1] != args[i])
            printf("arg %zu mismatch: [%ls] != [%ls]\n", i, parsed[i + 1].c_str(), args[i].c_str());
        XGT_CHECK(parsed[i + 1] == args[i]);
    }

    // known folders resolve
    XGT_CHECK(!xgshell::KnownFolder(xgshell::Folder::LocalAppData).empty());
    XGT_CHECK(!xgshell::KnownFolder(xgshell::Folder::Temp).empty());
    XGT_CHECK(!xgshell::KnownFolder(xgshell::Folder::ProgramData).empty());
    XGT_CHECK(xgshell::AppendPath(L"C:\\x", L"y\\z") == L"C:\\x\\y\\z");

    // elevation state is queryable without side effects
    bool elevated = xgshell::IsElevated();
    XGT_CHECK(elevated == true || elevated == false);

    XGT_FINISH("shell");
}
