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

#include <xgcfg/xgcfg.h>

#include "../test_util.h"

int main() {
    using namespace xgcfg;
    std::string err;

    // parse / dump roundtrip
    Value v;
    XGT_CHECK(Parse(R"({"a":1,"b":[1,2],"c":{"d":"x"}})", v, &err));
    XGT_CHECK(Dump(v, false) == R"({"a":1,"b":[1,2],"c":{"d":"x"}})");

    // loose input: comments + trailing comma
    Value loose;
    XGT_CHECK(Parse("{/*c*/\"n\":1,//x\n}", loose, &err));

    // errors carry an offset
    Value bad;
    XGT_CHECK(!Parse("{\"a\": }", bad, &err));
    XGT_CHECK(err.find("byte") != std::string::npos);

    // dot path write through arrays and objects
    Config cfg;
    XGT_CHECK(cfg.LoadText(R"({"ui":{"w":800}})", &err));
    XGT_CHECK(!cfg.Dirty());          // fresh load is clean
    XGT_CHECK(cfg.Set("ui.h", Value(static_cast<int64_t>(600))));
    XGT_CHECK(cfg.Set("mods[0].id", Value(std::string("a"))));
    XGT_CHECK(cfg.Set("mods[1].id", Value(std::string("b"))));
    XGT_CHECK(cfg.Dirty());           // writes mark it dirty
    XGT_CHECK(cfg.GetInt("ui.w") == 800);
    XGT_CHECK(cfg.GetString("mods[1].id") == "b");
    XGT_CHECK(cfg.GetInt("no.such", -1) == -1);

    // dirty flag: set raises, save clears, reload clears
    std::wstring dir = xgt::TempDir(L"cfg");
    std::wstring path = dir + L"\\c.json";
    XGT_CHECK(cfg.Set("ui.w", Value(static_cast<int64_t>(1024))));
    XGT_CHECK(cfg.Dirty());
    XGT_CHECK(cfg.SaveAs(path, true, &err));
    XGT_CHECK(!cfg.Dirty());
    XGT_CHECK(!cfg.Changed());
    XGT_CHECK(cfg.GetInt("ui.w") == 1024);

    // change callback: one hit per effective mutation, null payload on remove
    int hits = 0;
    bool sawNull = false;
    std::string lastPath;
    cfg.SetOnChange([&](const std::string& p, const Value& nv) {
        ++hits;
        lastPath = p;
        sawNull = nv.IsNull();
    });
    XGT_CHECK(cfg.Set("ui.h", Value(static_cast<int64_t>(700))));
    XGT_CHECK(cfg.Remove("ui.h"));
    XGT_CHECK(hits == 2);
    XGT_CHECK(lastPath == "ui.h" && sawNull);
    cfg.SetOnChange(nullptr);

    // reload
    XGT_CHECK(cfg.Reload(&err));
    XGT_CHECK(cfg.GetInt("ui.w") == 1024);

    // merge: default + user, keep vs overwrite
    Config def, usr;
    XGT_CHECK(def.LoadText(R"({"ui":{"theme":"dark","scale":1.0},"mods":["a"]})", &err));
    XGT_CHECK(usr.LoadText(R"({"ui":{"theme":"light"},"mods":["b","c"]})", &err));
    XGT_CHECK(def.Merge(usr, false) == 0);
    XGT_CHECK(def.GetString("ui.theme") == "dark");
    XGT_CHECK(def.Merge(usr, true) == 2);
    XGT_CHECK(def.GetString("ui.theme") == "light");
    XGT_CHECK(def.Root()["mods"].Size() == 2);

    // binder: pull keeps defaults for missing keys, push writes only diffs
    struct S {
        bool tray = true;
        int threads = 4;
        double scale = 1.0;
        std::string theme = "dark";
    } s;
    Config c2;
    XGT_CHECK(c2.LoadText(R"({"ui":{"theme":"light","threads":8}})", &err));
    Binder b(c2, "ui");
    b.BindBool("tray", s.tray).BindInt("threads", s.threads).BindDouble("scale", s.scale).BindString("theme", s.theme);
    XGT_CHECK(b.Pull() == 2);
    XGT_CHECK(s.theme == "light" && s.threads == 8 && s.tray && s.scale == 1.0);
    // tray and scale are absent from the config: Pull must leave the defaults alone,
    // so Push then has to create exactly those two keys.
    s.scale = 1.5;
    XGT_CHECK(b.Push() == 2);
    XGT_CHECK(c2.GetDouble("ui.scale") == 1.5);
    XGT_CHECK(c2.GetBool("ui.tray", false) == true);
    XGT_CHECK(b.Push() == 0);   // idempotent once everything is in sync

    xgt::DropDir(dir);
    XGT_FINISH("cfg");
}
