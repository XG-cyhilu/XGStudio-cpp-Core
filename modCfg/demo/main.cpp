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

#include <windows.h>

#include <cstdio>
#include <string>

int main() {
    // 1) 宽松解析：注释、尾逗号、转义、代理对
    std::string src = R"({
        // 行注释
        "name": "XGStudio",
        "version": 3,
        "ratio": 0.75,
        "enabled": true,
        "nothing": null,
        "list": [1, 2, 3,],
        /* 块注释 */
        "nested": { "deep": { "key": "值 \u4e2d\u6587 \ud83d\ude00" } },
    })";
    xgcfg::Value v;
    std::string err;
    if (!xgcfg::Parse(src, v, &err)) {
        printf("[1] parse failed: %s\n", err.c_str());
    } else {
        printf("[1] name=%s version=%lld ratio=%.2f enabled=%d null=%d list=%zu deep=%s\n", v["name"].AsString().c_str(),
               static_cast<long long>(v["version"].AsInt()), v["ratio"].AsDouble(), (int)v["enabled"].AsBool(),
               (int)v["nothing"].IsNull(), v.Size() == 0 ? 0 : v["list"].Size(),
               v.FindPath("nested.deep.key") ? v.FindPath("nested.deep.key")->AsString().c_str() : "-");
    }

    // 2) 错误定位
    xgcfg::Value bad;
    if (!xgcfg::Parse("{\"a\": }", bad, &err)) printf("[2] %s\n", err.c_str());

    // 3) 紧凑/美化输出
    printf("[3] compact: %s\n", xgcfg::Dump(v, false).c_str());

    // 4) Config：加载 → 读默认 → 改 → 原子保存 → 变更检测 → 重载
    std::wstring path = L"xgcfg_demo.json";
    xgcfg::Config cfg;
    if (!cfg.LoadText(xgcfg::Dump(v, true), &err)) printf("[4] loadtext failed: %s\n", err.c_str());
    printf("[4] GetBool=%d GetInt=%lld GetString=%s DefInt=%lld\n", (int)cfg.GetBool("enabled"),
           static_cast<long long>(cfg.GetInt("version")), cfg.GetString("name").c_str(),
           static_cast<long long>(cfg.GetInt("no.such.key", -1)));

    cfg.Set("ui.theme", xgcfg::Value(std::string("dark")));
    cfg.Set("ui.scale", xgcfg::Value(1.25));
    cfg.Set("mods[0].id", xgcfg::Value(std::string("fabric-api")));
    cfg.Set("mods[1].id", xgcfg::Value(std::string("sodium")));
    cfg.Set("mods[1].enabled", xgcfg::Value(false));
    cfg.Remove("nothing");
    cfg.Set("version", xgcfg::Value(static_cast<int64_t>(4)));

    if (!cfg.SaveAs(path, true, &err)) {
        printf("[4] save failed: %s\n", err.c_str());
        return 1;
    }
    std::string saved;
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f) {
            char buf[4096];
            size_t n = fread(buf, 1, sizeof buf, f);
            saved.assign(buf, n);
            fclose(f);
        }
    }
    printf("[4] saved %zu bytes, mods[0]=%s theme=%s\n", saved.size(),
           cfg.GetString("mods[0].id").c_str(), cfg.GetString("ui.theme").c_str());

    // 5) 热重载：手改文件内容 → Changed() → Reload()
    printf("[5] changed before touch = %d\n", (int)cfg.Changed());
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f) {
            fputs("{\"name\":\"edited\",\"version\":9}", f);
            fclose(f);
        }
    }
    Sleep(20);
    bool changed = cfg.Changed();
    bool reloaded = cfg.Reload(&err);
    printf("[5] changed=%d reloaded=%d name=%s version=%lld\n", (int)changed, (int)reloaded, cfg.GetString("name").c_str(),
           static_cast<long long>(cfg.GetInt("version")));

    // 6) 往返：Dump → Parse 稳定
    xgcfg::Value again;
    bool roundtrip = xgcfg::Parse(xgcfg::Dump(cfg.Root(), true), again, &err) &&
                     xgcfg::Dump(again, false) == xgcfg::Dump(cfg.Root(), false);
    printf("[6] roundtrip stable = %d\n", (int)roundtrip);

    _wremove(path.c_str());
    return 0;
}
