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

// Writes real minidumps, but never crashes on purpose: an actual access violation
// in ctest would be indistinguishable from a test harness failure.

#include <xgcrash/xgcrash.h>

#include "../test_util.h"

#include <filesystem>

int main() {
    std::wstring dir = xgt::TempDir(L"crash");
    XGT_CHECK(!xgcrash::DefaultDumpDir().empty());

    xgcrash::Options opt;
    opt.dumpDir = dir;
    opt.appName = L"xgtcrash";
    opt.maxDumps = 0;          // keep every dump for this check, prune manually below
    opt.catchVectored = false;
    XGT_CHECK(xgcrash::Install(opt));

    std::wstring p1, p2;
    XGT_CHECK(xgcrash::WriteDump(nullptr, L"t1", &p1));
    XGT_CHECK(!p1.empty());
    XGT_CHECK(std::filesystem::exists(p1));
    XGT_CHECK(std::filesystem::file_size(p1) > 4096);   // a real dump, not an empty file

    // second dump in the same second must not clobber the first
    XGT_CHECK(xgcrash::WriteDump(nullptr, L"t2", &p2));
    XGT_CHECK(p1 != p2);

    auto dumps = xgcrash::ListDumps();
    XGT_CHECK(dumps.size() == 2);
    XGT_CHECK(xgcrash::PruneDumps(1) == 1);
    XGT_CHECK(xgcrash::ListDumps().size() == 1);

    xgcrash::Shutdown();

    xgt::DropDir(dir);
    XGT_FINISH("crash");
}
