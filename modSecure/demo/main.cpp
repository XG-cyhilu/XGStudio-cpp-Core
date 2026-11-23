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

#include <cstdio>
#include <string>

int main() {
    // 1) Base64：RFC 4648 的经典向量 + URL 变体 + 解码回环
    std::string b64 = xgsecure::Base64Encode("hello world", 11);
    printf("[1] base64    = %s\n", b64.c_str());
    const char twoBytes[2] = { '\xfb', '\xff' };
    printf("[1] base64url = %s\n", xgsecure::Base64UrlEncode(twoBytes, 2).c_str());

    std::vector<uint8_t> dec;
    bool decOk = xgsecure::Base64Decode(b64, dec);
    std::string round(dec.begin(), dec.end());
    bool urlOk = xgsecure::Base64Decode("-_-_", dec);          // base64url 无填充 + 混用字母表
    printf("[1] decode=%d text=%s urlDecode=%d\n", (int)decOk, round.c_str(), (int)urlOk);

    // 2) 随机：不同长度的十六进制 / base64url
    printf("[2] random hex=%s len=%zu\n", xgsecure::RandomHex(8).c_str(), xgsecure::RandomHex(8).size());
    std::string r1 = xgsecure::RandomBase64Url(16), r2 = xgsecure::RandomBase64Url(16);
    printf("[2] two draws differ=%d\n", (int)(r1 != r2));

    // 3) HMAC-SHA256：RFC 4231 Test Case 2
    std::string mac = xgsecure::HmacSha256Hex("key", "The quick brown fox jumps over the lazy dog");
    printf("[3] hmac=%s\n", mac.c_str());
    printf("[3] expected=%s verify=%d badVerify=%d\n",
           "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8",
           (int)xgsecure::HmacSha256Verify("key", "The quick brown fox jumps over the lazy dog", mac),
           (int)xgsecure::HmacSha256Verify("key2", "The quick brown fox jumps over the lazy dog", mac));

    // 4) AES-GCM：加解密往返、错 key 拒绝、密文被改拒绝、AAD 不匹配拒绝
    std::vector<uint8_t> key = xgsecure::NewAesKey();
    std::string plain = "refresh_token=abc123";
    std::string sealed, opened;
    bool sealOk = xgsecure::SealText(key, plain, sealed);
    bool openOk = xgsecure::UnsealText(key, sealed, opened);
    printf("[4] sealedLen=%zu seal=%d open=%d plain=%s\n", sealed.size(), (int)sealOk, (int)openOk, opened.c_str());

    std::vector<uint8_t> wrongKey = xgsecure::NewAesKey();
    std::string junk;
    printf("[4] wrongKeyAccepted=%d\n", (int)xgsecure::UnsealText(wrongKey, sealed, junk));

    std::string tampered = sealed;
    tampered[tampered.size() - 2] = (tampered[tampered.size() - 2] == 'A') ? 'B' : 'A';
    printf("[4] tamperAccepted=%d\n", (int)xgsecure::UnsealText(key, tampered, junk));

    std::string withAad;
    xgsecure::SealText(key, plain, withAad, "vcl/account");
    printf("[4] aadMismatchAccepted=%d\n", (int)xgsecure::UnsealText(key, withAad, junk, "vcl/other"));
    printf("[4] aadMatch=%d\n", (int)xgsecure::UnsealText(key, withAad, junk, "vcl/account"));

    // 5) PBKDF2-HMAC-SHA256：RFC 7914 §11 向量（c=1, dkLen=32）
    std::vector<uint8_t> salt{ 's', 'a', 'l', 't' };
    std::vector<uint8_t> dk = xgsecure::KeyFromPassphrase("password", salt, 1);
    printf("[5] pbkdf2=%s\n", xgsecure::Base64Encode(dk).c_str());
    printf("[5] expected(hex)=%s\n", "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    return 0;
}
