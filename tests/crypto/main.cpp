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

#include "../test_util.h"

int main() {
    using xgcrypto::Alg;
    const char* abc = "abc";

    // well-known vectors
    XGT_CHECK(xgcrypto::HashHex(Alg::Md5, abc, 3) == "900150983cd24fb0d6963f7d28e17f72");
    XGT_CHECK(xgcrypto::HashHex(Alg::Sha1, abc, 3) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    XGT_CHECK(xgcrypto::HashHex(Alg::Sha256, abc, 3) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    XGT_CHECK(xgcrypto::HashHex(Alg::Sha512, abc, 3) ==
              "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
              "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

    // digest sizes
    XGT_CHECK(xgcrypto::DigestSize(Alg::Md5) == 16);
    XGT_CHECK(xgcrypto::DigestSize(Alg::Sha1) == 20);
    XGT_CHECK(xgcrypto::DigestSize(Alg::Sha256) == 32);
    XGT_CHECK(xgcrypto::DigestSize(Alg::Sha384) == 48);
    XGT_CHECK(xgcrypto::DigestSize(Alg::Sha512) == 64);
    XGT_CHECK(xgcrypto::Hash(Alg::Sha256, abc, 3).size() == 32);

    // incremental == one-shot
    xgcrypto::Digest d(Alg::Sha256);
    XGT_CHECK(d.Update("a", 1));
    XGT_CHECK(d.Update("bc", 2));
    std::vector<uint8_t> out;
    XGT_CHECK(d.Final(out));
    XGT_CHECK(xgcrypto::ToHex(out) == xgcrypto::HashHex(Alg::Sha256, abc, 3));
    // Final is idempotent
    std::vector<uint8_t> again;
    XGT_CHECK(d.Final(again) && again == out);

    // hex roundtrip + rejects bad input
    XGT_CHECK(xgcrypto::FromHex("00ff7f") == std::vector<uint8_t>({0x00, 0xff, 0x7f}));
    XGT_CHECK(xgcrypto::FromHex("0g").empty());
    XGT_CHECK(xgcrypto::FromHex("abc").empty());   // odd length

    // file hash matches byte hash
    std::wstring dir = xgt::TempDir(L"crypto");
    std::wstring path = dir + L"\\abc.bin";
    {
        std::ofstream f(path, std::ios::binary);
        f.write(abc, 3);
    }
    XGT_CHECK(xgcrypto::HashFile(Alg::Sha256, path) == xgcrypto::Hash(Alg::Sha256, abc, 3));

    xgt::DropDir(dir);
    XGT_FINISH("crypto");
}
