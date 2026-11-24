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

#include "deflate.h"

#include <cstring>

namespace xgzip {
namespace detail {

namespace {

constexpr int kWindow = 32768;
constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 258;
constexpr int kHashBits = 15;
constexpr int kHashSize = 1 << kHashBits;
constexpr int kNone = -1;

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                               35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                               3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193,
                                12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}

    void Bits(uint32_t value, int n) {           // LSB 优先（deflate 的 extra bits、块头）
        acc_ |= (uint64_t)value << count_;
        count_ += n;
        while (count_ >= 8) {
            out_.push_back((uint8_t)(acc_ & 0xFF));
            acc_ >>= 8;
            count_ -= 8;
        }
    }

    void Code(uint32_t code, int n) {            // Huffman 码字是 MSB 优先，这里翻过来
        for (int i = n - 1; i >= 0; --i) Bits((code >> i) & 1u, 1);
    }

    void Flush() {
        if (count_) {
            out_.push_back((uint8_t)(acc_ & 0xFF));
            acc_ = 0;
            count_ = 0;
        }
    }

private:
    std::vector<uint8_t>& out_;
    uint64_t acc_ = 0;
    int count_ = 0;
};

void LiteralCode(uint32_t sym, uint32_t& code, int& bits) {
    if (sym < 144) { code = 0x30 + sym; bits = 8; }
    else if (sym < 256) { code = 0x190 + (sym - 144); bits = 9; }
    else if (sym < 280) { code = sym - 256; bits = 7; }
    else { code = 0xC0 + (sym - 280); bits = 8; }
}

int LengthCode(int len) {
    // 返回 257..285 的下标
    for (int i = 28; i >= 0; --i)
        if (len >= kLenBase[i]) return i;
    return 0;
}

int DistCode(int dist) {
    for (int i = 29; i >= 0; --i)
        if (dist >= kDistBase[i]) return i;
    return 0;
}

uint32_t Hash3(const uint8_t* p) {
    uint32_t h = (uint32_t)p[0] * 0x1E35A7BDu + (uint32_t)p[1] * 0x9E3779B1u + (uint32_t)p[2];
    return (h >> (32 - kHashBits)) & (kHashSize - 1);
}

} // namespace

bool DeflateFixed(const uint8_t* data, size_t len, std::vector<uint8_t>& out, int effort) {
    out.clear();
    if (effort < 1) effort = 1;
    if (effort > 9) effort = 9;
    const int maxChain = 4 + effort * 12;

    int* head = new (std::nothrow) int[kHashSize];
    int* prev = new (std::nothrow) int[kWindow];
    if (!head || !prev) {
        delete[] head;
        delete[] prev;
        return false;
    }
    for (int i = 0; i < kHashSize; ++i) head[i] = kNone;
    for (int i = 0; i < kWindow; ++i) prev[i] = kNone;

    BitWriter bw(out);
    bw.Bits(1, 1);   // BFINAL
    bw.Bits(1, 2);   // BTYPE = 01 固定 Huffman

    size_t pos = 0;
    while (pos < len) {
        int bestLen = 0, bestDist = 0;
        if (pos + kMinMatch <= len) {
            uint32_t h = Hash3(data + pos);
            int cand = head[h];
            int chain = 0;
            int limit = (int)(pos > (size_t)kWindow ? kWindow : pos);
            while (cand != kNone && chain++ < maxChain) {
                int dist = (int)pos - cand;
                if (dist <= 0 || dist > kWindow) break;
                const uint8_t* a = data + pos;
                const uint8_t* b = data + cand;
                size_t maxLen = len - pos;
                if (maxLen > kMaxMatch) maxLen = kMaxMatch;
                size_t l = 0;
                while (l < maxLen && a[l] == b[l]) ++l;
                if ((int)l > bestLen && l >= kMinMatch) {
                    bestLen = (int)l;
                    bestDist = dist;
                    if (bestLen == kMaxMatch) break;
                }
                cand = prev[cand & (kWindow - 1)];
                if (--limit <= 0) break;
            }
        }

        if (bestLen >= kMinMatch) {
            int lc = LengthCode(bestLen);
            uint32_t code;
            int bits;
            LiteralCode(257 + lc, code, bits);
            bw.Code(code, bits);
            int extra = kLenExtra[lc];
            if (extra) bw.Bits((uint32_t)(bestLen - kLenBase[lc]), extra);

            int dc = DistCode(bestDist);
            bw.Code((uint32_t)dc, 5);   // 固定距离码：5 位，值就是符号
            extra = kDistExtra[dc];
            if (extra) bw.Bits((uint32_t)(bestDist - kDistBase[dc]), extra);

            for (int i = 0; i < bestLen; ++i) {
                size_t p = pos + i;
                if (p + kMinMatch <= len) {
                    uint32_t h = Hash3(data + p);
                    prev[p & (kWindow - 1)] = head[h];
                    head[h] = (int)p;
                }
            }
            pos += bestLen;
        } else {
            uint32_t code;
            int bits;
            LiteralCode((uint32_t)data[pos], code, bits);
            bw.Code(code, bits);
            if (pos + kMinMatch <= len) {
                uint32_t h = Hash3(data + pos);
                prev[pos & (kWindow - 1)] = head[h];
                head[h] = (int)pos;
            }
            ++pos;
        }
    }

    uint32_t code;
    int bits;
    LiteralCode(256, code, bits);   // end of block
    bw.Code(code, bits);
    bw.Flush();

    delete[] head;
    delete[] prev;
    return true;
}

} // namespace detail
} // namespace xgzip
