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

#include <xgproc/xgproc.h>

#include "../test_util.h"

int main() {
    // 1) stdout capture + exit code
    {
        xgproc::Process p;
        XGT_CHECK(p.Start(L"cmd.exe /c echo xgt-proc-ok"));
        xgproc::Result r = p.Drain(10000);
        XGT_CHECK(r.started);
        XGT_CHECK(r.exitCode == 0);
        std::string all;
        for (const auto& l : r.out) all += l.text + "\n";
        XGT_CHECK(all.find("xgt-proc-ok") != std::string::npos);
    }

    // 2) non-zero exit code is reported, not swallowed
    {
        xgproc::Process p;
        XGT_CHECK(p.Start(L"cmd.exe /c exit 3"));
        xgproc::Result r = p.Drain(10000);
        XGT_CHECK(r.exitCode == 3);
    }

    // 3) stderr is captured and tagged
    {
        xgproc::Process p;
        XGT_CHECK(p.Start(L"cmd.exe /c \"echo boom 1>&2\""));
        xgproc::Result r = p.Drain(10000);
        bool sawStderr = false, sawText = false;
        for (const auto& l : r.out) {
            if (l.stderrLine) sawStderr = true;
            if (l.text.find("boom") != std::string::npos) sawText = true;
        }
        XGT_CHECK(r.exitCode == 0);
        XGT_CHECK(sawText);
        XGT_CHECK(sawStderr);
    }

    // 4) env override reaches the child
    {
        xgproc::Options opt;
        opt.commandLine = L"cmd.exe /c echo %XGT_PROBE%";
        opt.env.push_back({ L"XGT_PROBE", L"probe-value" });
        xgproc::Process p;
        XGT_CHECK(p.Start(opt));
        xgproc::Result r = p.Drain(10000);
        std::string all;
        for (const auto& l : r.out) all += l.text;
        XGT_CHECK(all.find("probe-value") != std::string::npos);
    }

    // 5) a long-running child is Stop()-ped, and the tree really dies
    {
        xgproc::Process p;
        XGT_CHECK(p.Start(L"cmd.exe /c ping -n 30 127.0.0.1 > nul"));
        XGT_CHECK(p.IsRunning());
        p.Stop(500);
        XGT_CHECK(!p.IsRunning());
    }

    XGT_FINISH("proc");
}
