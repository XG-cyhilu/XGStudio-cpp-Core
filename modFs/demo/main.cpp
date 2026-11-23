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

#include <xgfs/xgfs.h>

#include <cstdio>
#include <string>

int main() {
    // 1) 路径工具
    printf("[1] normalize = %ls\n", xgfs::Normalize(L"..\\xgfs\\a//b\\\\c\\").c_str());
    printf("[1] join      = %ls\n", xgfs::Join(L"D:\\root", L"sub/../file.txt").c_str());
    printf("[1] parts     = %ls | %ls | %ls | %ls\n", xgfs::FileName(L"D:\\a\\b\\mod.jar").c_str(),
           xgfs::Stem(L"D:\\a\\b\\mod.jar").c_str(), xgfs::Extension(L"D:\\a\\b\\mod.jar").c_str(),
           xgfs::ReplaceExtension(L"D:\\a\\b\\mod.jar", L"json").c_str());
    printf("[1] parent    = %ls\n", xgfs::Parent(L"D:\\a\\b\\mod.jar").c_str());

    std::wstring dir = xgfs::Join(xgfs::Normalize(L"."), L"xgfs_demo_dir");
    xgfs::Remove(dir, true);
    std::string err;
    if (!xgfs::EnsureDir(dir + L"\\deep\\er", &err)) {
        printf("ensure dir failed: %s\n", err.c_str());
        return 1;
    }

    // 2) 普通写 / 原子写
    const std::string text = "line1\nline2\n";
    if (!xgfs::WriteFile(dir + L"\\plain.txt", text, &err)) printf("write failed: %s\n", err.c_str());
    if (!xgfs::WriteFileAtomic(dir + L"\\atomic.json", std::string("{\"k\":1}"), &err))
        printf("atomic failed: %s\n", err.c_str());
    std::string back;
    printf("[2] plain=%zu atomic=%zu\n", (xgfs::ReadFile(dir + L"\\plain.txt", back, &err) ? back.size() : 0),
           (xgfs::ReadFile(dir + L"\\atomic.json", back, &err) ? back.size() : 0));

    // 3) 大小 / 时间
    uint64_t sz = 0;
    int64_t mt = 0;
    bool okSize = xgfs::Size(dir + L"\\plain.txt", sz);
    xgfs::ModifiedTime(dir + L"\\plain.txt", mt);
    printf("[3] size=%d bytes=%llu mtime=%lld\n", (int)okSize, static_cast<unsigned long long>(sz),
           static_cast<long long>(mt));

    // 4) 递归遍历 + 通配
    for (int i = 0; i < 5; ++i)
        xgfs::WriteFile(dir + L"\\deep\\f" + std::to_wstring(i) + L".txt", std::string(i + 1, 'x'), nullptr);
    std::vector<xgfs::DirEntry> all, onlyTxt;
    xgfs::ListOptions o1;
    o1.recursive = true;
    xgfs::ListDir(dir, all, o1, &err);
    xgfs::ListOptions o2;
    o2.pattern = L"*.json";
    xgfs::ListDir(dir, onlyTxt, o2, &err);
    printf("[4] recursive=%zu files, *.json=%zu (", all.size(), onlyTxt.size());
    for (const auto& e : onlyTxt) printf("%ls ", e.name.c_str());
    printf(")\n");

    // 5) 复制 / 移动 / 删除
    bool okCopy = xgfs::Copy(dir + L"\\plain.txt", dir + L"\\copy.txt", false, &err);
    bool okMove = xgfs::Move(dir + L"\\copy.txt", dir + L"\\moved.txt", true, &err);
    printf("[5] copy=%d move=%d err=%s\n", (int)okCopy, (int)okMove, err.c_str());
    xgfs::Remove(dir + L"\\moved.txt", false, &err);
    printf("[5] after remove exists=%d\n", (int)xgfs::Exists(dir + L"\\moved.txt"));

    // 6) 临时文件 / 临时目录
    std::wstring tmp;
    if (xgfs::CreateTempFile(dir, tmp, &err)) {
        printf("[6] temp=%ls exists=%d\n", xgfs::FileName(tmp).c_str(), (int)xgfs::Exists(tmp));
        xgfs::Remove(tmp, false, &err);
    }
    std::wstring tmpDir;
    if (xgfs::CreateTempDir(dir, tmpDir, &err)) {
        printf("[6] tempdir=%ls isdir=%d\n", xgfs::FileName(tmpDir).c_str(), (int)xgfs::IsDir(tmpDir));
        xgfs::RemoveWithRetry(tmpDir, true, 3, 50, &err);
    }

    // 7) 统计 / 容量 / 长路径
    uint64_t total = 0;
    bool sizeOk = xgfs::DirSize(dir, total, &err);   // 先算，printf 的参数求值顺序是不确定的
    printf("[7] dirsize=%d bytes=%llu\n", (int)sizeOk, static_cast<unsigned long long>(total));
    uint64_t freeB = 0, totB = 0;
    bool diskOk = xgfs::DiskSpace(dir, freeB, totB, &err);
    printf("[7] disk free=%.1f GB total=%.1f GB ok=%d\n", double(freeB) / 1073741824.0,
           double(totB) / 1073741824.0, (int)diskOk);

    // 构造一条 300 字符的深层路径（超过 MAX_PATH），验证长路径前缀确实生效
    std::wstring deep = dir;
    while (deep.size() < 300) deep += L"\\segment_0123456789";
    deep += L"\\leaf.txt";
    std::wstring lp = xgfs::LongPath(deep);
    bool longOk = xgfs::WriteFileAtomic(deep, std::string("long path"), &err);
    std::string lpText;
    bool longRead = xgfs::ReadFile(deep, lpText, &err);
    printf("[7] longpath len=%zu prefix=%d write=%d read=%zu\n", lp.size(), (int)lp.rfind(L"\\\\?\\") == 0, (int)longOk,
           lpText.size());

    // 8) 原子目录替换
    std::wstring a = dir + L"\\stage_a", b = dir + L"\\target_b";
    xgfs::EnsureDir(a, &err);
    xgfs::WriteFile(a + L"\\v.txt", std::string("v2"), &err);
    xgfs::EnsureDir(b, &err);
    xgfs::WriteFile(b + L"\\v.txt", std::string("v1"), &err);
    bool replaced = xgfs::ReplaceDirectory(a, b, &err);
    std::string v;
    xgfs::ReadFile(b + L"\\v.txt", v, &err);
    printf("[8] replace=%d content=%s leftoverOld=%d\n", (int)replaced, v.c_str(), (int)xgfs::Exists(b + L".old"));

    xgfs::Remove(dir, true);
    printf("[9] cleanup exists=%d\n", (int)xgfs::Exists(dir));
    return 0;
}
