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

#include <xgsecure/xgsecure.h>

#include "../test_util.h"

int main() {
    // Base64 roundtrip + URL-safe variant has no padding and no +/
    XGT_CHECK(xgsecure::Base64Encode("hello world", 11) == "aGVsbG8gd29ybGQ=");

    const std::string plain = "hello world / ? + ~";
    std::string b64 = xgsecure::Base64Encode(plain.data(), plain.size());
    XGT_CHECK(b64.size() == (plain.size() + 2) / 3 * 4);
    std::vector<uint8_t> dec;
    XGT_CHECK(xgsecure::Base64Decode(b64, dec));
    XGT_CHECK(std::string(dec.begin(), dec.end()) == plain);

    std::string url64 = xgsecure::Base64UrlEncode(plain.data(), plain.size());
    XGT_CHECK(url64.find('=') == std::string::npos);
    XGT_CHECK(url64.find('+') == std::string::npos && url64.find('/') == std::string::npos);
    dec.clear();
    XGT_CHECK(xgsecure::Base64Decode(url64, dec));
    XGT_CHECK(std::string(dec.begin(), dec.end()) == plain);

    XGT_CHECK(!xgsecure::Base64Decode("!!!!", dec));

    // random: right length, not all zero
    auto rnd = xgsecure::RandomBytes(16);
    XGT_CHECK(rnd.size() == 16);
    bool any = false;
    for (auto b : rnd) any = any || b != 0;
    XGT_CHECK(any);
    XGT_CHECK(xgsecure::RandomHex(8).size() == 16);

    // HMAC-SHA256 known vector (RFC 4231 style, key "key")
    XGT_CHECK(xgsecure::HmacSha256Hex("key", "The quick brown fox jumps over the lazy dog") ==
              "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
    XGT_CHECK(xgsecure::HmacSha256Verify("key", "The quick brown fox jumps over the lazy dog",
                                         "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"));
    XGT_CHECK(!xgsecure::HmacSha256Verify("key", "tampered",
                                         "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"));

    // AES-256-GCM: seal/unseal text, tamper detection, AAD must match
    auto key = xgsecure::NewAesKey();
    XGT_CHECK(key.size() == 32);
    std::string sealed, opened;
    XGT_CHECK(xgsecure::SealText(key, "secret payload", sealed, "ctx"));
    XGT_CHECK(xgsecure::UnsealText(key, sealed, opened, "ctx") && opened == "secret payload");
    XGT_CHECK(!xgsecure::UnsealText(key, sealed, opened, "other"));   // wrong AAD

    std::string flipped = sealed;
    flipped[20] ^= 0x01;                                              // corrupt ciphertext/tag area
    XGT_CHECK(!xgsecure::UnsealText(key, flipped, opened, "ctx"));

    auto key2 = xgsecure::NewAesKey();
    XGT_CHECK(!xgsecure::UnsealText(key2, sealed, opened, "ctx"));     // wrong key

    // same plaintext seals to different bytes (fresh IV each time)
    std::string sealed2;
    XGT_CHECK(xgsecure::SealText(key, "secret payload", sealed2, "ctx"));
    XGT_CHECK(sealed != sealed2);

    // PBKDF2: deterministic, right length, iteration count matters
    std::vector<uint8_t> salt = {1, 2, 3, 4, 5, 6, 7, 8};
    auto k1 = xgsecure::KeyFromPassphrase("passphrase", salt, 1000);
    auto k2 = xgsecure::KeyFromPassphrase("passphrase", salt, 1000);
    auto k3 = xgsecure::KeyFromPassphrase("passphrase", salt, 2000);
    XGT_CHECK(k1.size() == 32);
    XGT_CHECK(k1 == k2);
    XGT_CHECK(k1 != k3);

    XGT_FINISH("secure");
}
