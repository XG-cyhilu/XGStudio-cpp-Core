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

#include "../test_util.h"

int main() {
    std::string err;
    std::wstring dir = xgt::TempDir(L"fs");
    std::wstring file = dir + L"\\a.txt";

    XGT_CHECK(xgfs::EnsureDir(dir + L"\\sub", &err));
    XGT_CHECK(xgfs::IsDir(dir + L"\\sub"));

    // atomic write then read
    std::string payload(1000, 'x');
    XGT_CHECK(xgfs::WriteFileAtomic(file, payload, &err));
    XGT_CHECK(xgfs::Exists(file) && xgfs::IsFile(file));
    uint64_t bytes = 0;
    XGT_CHECK(xgfs::Size(file, bytes) && bytes == payload.size());
    std::string back;
    XGT_CHECK(xgfs::ReadFile(file, back, &err) && back == payload);

    // no stray temp file left behind in the directory
    uint64_t dirBytes = 0;
    XGT_CHECK(xgfs::DirSize(dir, dirBytes, &err) && dirBytes == payload.size());

    // path helpers
    XGT_CHECK(xgfs::FileName(file) == L"a.txt");
    XGT_CHECK(xgfs::Stem(file) == L"a");
    XGT_CHECK(xgfs::Extension(file) == L".txt");
    XGT_CHECK(xgfs::Parent(file) == dir);
    XGT_CHECK(xgfs::Join(dir, L"b.txt") == dir + L"\\b.txt");
    XGT_CHECK(xgfs::ReplaceExtension(file, L"log") == dir + L"\\a.log");

    // overwrite writes are atomic: old content must never be visible half-written
    XGT_CHECK(xgfs::WriteFileAtomic(file, std::string("second"), &err));
    XGT_CHECK(xgfs::ReadFile(file, back, &err) && back == "second");

    // move / copy
    std::wstring copyPath = dir + L"\\c.txt";
    XGT_CHECK(xgfs::Copy(file, copyPath, false, &err));
    XGT_CHECK(xgfs::Exists(copyPath));

    // disk space is reportable for this drive
    uint64_t freeBytes = 0, totalBytes = 0;
    XGT_CHECK(xgfs::DiskSpace(dir, freeBytes, totalBytes, &err));
    XGT_CHECK(totalBytes > 0);

    // delete removes it
    XGT_CHECK(xgfs::Remove(file));
    XGT_CHECK(!xgfs::Exists(file));

    xgt::DropDir(dir);
    XGT_FINISH("fs");
}
