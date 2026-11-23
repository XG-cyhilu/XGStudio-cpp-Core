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

#include <xgcrypto/xgcrypto.h>

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

int main() {
    // 1) 已知向量：空串 / "abc"
    const char* abc = "abc";
    printf("[1] sha256(\"\")  = %s\n", xgcrypto::HashHex(xgcrypto::Alg::Sha256, nullptr, 0).c_str());
    printf("[1] sha256(abc) = %s\n", xgcrypto::HashHex(xgcrypto::Alg::Sha256, abc, 3).c_str());
    printf("[1] sha1(abc)   = %s\n", xgcrypto::HashHex(xgcrypto::Alg::Sha1, abc, 3).c_str());
    printf("[1] md5(abc)    = %s\n", xgcrypto::HashHex(xgcrypto::Alg::Md5, abc, 3).c_str());
    printf("[1] sha384(\"\")  = %s\n", xgcrypto::HashHex(xgcrypto::Alg::Sha384, nullptr, 0).c_str());
    printf("[1] sha384(abc) = %s\n", xgcrypto::HashHex(xgcrypto::Alg::Sha384, abc, 3).c_str());
    printf("[1] sha512(\"\")  = %s\n", xgcrypto::HashHex(xgcrypto::Alg::Sha512, nullptr, 0).c_str());
    printf("[1] sha512(abc) = %s\n", xgcrypto::HashHex(xgcrypto::Alg::Sha512, abc, 3).c_str());

    // 2) 分块喂入 == 一次喂入
    std::string big(3 * 1024 * 1024, '\0');
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>((i * 7 + 3) & 0xFF);
    auto once = xgcrypto::HashHex(xgcrypto::Alg::Sha256, big.data(), big.size());
    xgcrypto::Digest d(xgcrypto::Alg::Sha256);
    for (size_t off = 0; off < big.size(); off += 4096) {
        size_t n = (off + 4096 > big.size()) ? big.size() - off : 4096;
        d.Update(big.data() + off, n);
    }
    std::vector<uint8_t> chunked;
    d.Final(chunked);
    printf("[2] chunked == once : %d (%s)\n", once == xgcrypto::ToHex(chunked), once.c_str());

    // 3) 文件哈希
    std::wstring path = L"xgcrypto_demo.tmp";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f) {
        fwrite(big.data(), 1, big.size(), f);
        fclose(f);
        auto fileHash = xgcrypto::HashFileHex(xgcrypto::Alg::Sha256, path);
        printf("[3] file == memory : %d (%s)\n", fileHash == once, fileHash.c_str());
        _wremove(path.c_str());
    }

    // 4) hex 往返 + 定长比较
    auto bytes = xgcrypto::FromHex(once);
    printf("[4] roundtrip=%d size=%zu equals=%d badh=%d\n", xgcrypto::ToHex(bytes) == once, bytes.size(),
           (int)xgcrypto::Equals(bytes, xgcrypto::FromHex(once)), (int)xgcrypto::FromHex("xyz").empty());

    // 5) 与系统工具口径一致（certutil 用大写，比较前转小写）
    printf("[5] sha1 empty = %s\n", xgcrypto::HashHex(xgcrypto::Alg::Sha1, nullptr, 0).c_str());

    // 6) 长度/名字 + SHA-512 的块边界（1023 字节 = 8 个 128 字节块差 1，补位跨块）
    for (auto a : { xgcrypto::Alg::Md5, xgcrypto::Alg::Sha1, xgcrypto::Alg::Sha256, xgcrypto::Alg::Sha384,
                    xgcrypto::Alg::Sha512 })
        printf("[6] %-7s size=%u\n", xgcrypto::AlgName(a), xgcrypto::DigestSize(a));

    std::string b1023(1023, 'Z');
    std::vector<uint8_t> once512 = xgcrypto::Hash(xgcrypto::Alg::Sha512, b1023.data(), b1023.size());
    xgcrypto::Digest d512(xgcrypto::Alg::Sha512);
    for (size_t off = 0; off < b1023.size(); off += 37) {
        size_t n = (off + 37 > b1023.size()) ? b1023.size() - off : 37;
        d512.Update(b1023.data() + off, n);
    }
    std::vector<uint8_t> chunk512;
    d512.Final(chunk512);
    printf("[6] sha512 1023B chunked==once: %d\n", (int)xgcrypto::Equals(once512, chunk512));
    return 0;
}
