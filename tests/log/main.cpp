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

#include <xglog/xglog.h>

#include "../test_util.h"

int main() {
    std::wstring dir = xgt::TempDir(L"log");

    xglog::Config cfg;
    cfg.dir = dir;
    cfg.baseName = L"xgtlog";
    cfg.console = false;      // keep ctest output clean
    cfg.debugger = false;
    cfg.file = true;
    cfg.level = xglog::Level::Trace;
    cfg.fileLevel = xglog::Level::Trace;
    XGT_CHECK(xglog::Init(cfg));
    XGT_CHECK(xglog::Init(cfg));   // idempotent

    XGLOG_INFO("xgt-marker-%d", 424242);
    XGLOG_WARN("warn line");
    XGLOG_ERROR("error %s", "line");
    xglog::Flush();

    // UI polling sees the newest records
    xglog::Record rec;
    int seen = 0;
    std::string all;
    while (xglog::UiPoll(rec) && seen < 64) {
        all += rec.text;
        ++seen;
    }
    XGT_CHECK(seen >= 3);
    XGT_CHECK(all.find("xgt-marker-424242") != std::string::npos);

    XGT_CHECK(xglog::Dropped() == 0);
    XGT_CHECK(xglog::GetLevel() == xglog::Level::Trace);

    xglog::SetLevel(xglog::Level::Warn);
    XGT_CHECK(xglog::GetLevel() == xglog::Level::Warn);
    xglog::SetLevel(xglog::Level::Trace);

    xglog::Shutdown();

    // the file sink actually hit disk with the marker
    std::string disk;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (e.is_regular_file()) disk += xgt::ReadAll(e.path().wstring());
    }
    XGT_CHECK(disk.find("xgt-marker-424242") != std::string::npos);
    XGT_CHECK(disk.find("error line") != std::string::npos);

    xgt::DropDir(dir);
    XGT_FINISH("log");
}
