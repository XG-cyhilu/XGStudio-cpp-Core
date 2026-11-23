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

#include "inflate.h"

#include <cstring>
#include <vector>

namespace xgzip {
namespace detail {
namespace {

const uint16_t kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const uint8_t kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const uint16_t kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const uint8_t kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

struct Huffman {
    uint16_t counts[16];
    uint16_t symbols[288];

    void Clear() { memset(counts, 0, sizeof counts); }
};

// 建规范 Huffman 表；返回未配平的码数（0 = 完整），负数 = 超配（非法）
int Build(Huffman& h, const uint8_t* lens, int n) {
    h.Clear();
    for (int i = 0; i < n; ++i) ++h.counts[lens[i]];

    int left = 1;
    for (int len = 1; len <= 15; ++len) {
        left <<= 1;
        left -= h.counts[len];
        if (left < 0) return -1;
    }

    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<uint16_t>(offs[len] + h.counts[len]);
    for (int i = 0; i < n; ++i)
        if (lens[i]) h.symbols[offs[lens[i]]++] = static_cast<uint16_t>(i);
    return left;
}

class BitReader {
public:
    explicit BitReader(ByteSource& src) : src_(src) {}

    bool Get(int n, uint32_t& out) {
        while (bits_ < n) {
            if (cpos_ == cend_ && !Refill()) return false;
            acc_ |= static_cast<uint32_t>(chunk_[cpos_++]) << bits_;
            bits_ += 8;
        }
        out = acc_ & ((1u << n) - 1);
        acc_ >>= n;
        bits_ -= n;
        return true;
    }

    uint32_t GetUnchecked(int n) {
        uint32_t v = 0;
        Get(n, v);
        return v;
    }

    bool AlignByte() {
        acc_ = 0;
        bits_ = 0;
        return true;
    }

private:
    bool Refill() {
        size_t got = 0;
        if (!src_.Read(chunk_, sizeof chunk_, got) || got == 0) return false;
        cpos_ = 0;
        cend_ = got;
        return true;
    }

    ByteSource& src_;
    uint8_t chunk_[4096];
    size_t cpos_ = 0;
    size_t cend_ = 0;
    uint32_t acc_ = 0;
    int bits_ = 0;
};

// 逐位走的规范解码：表小、代码短，够用且不容易写错
int Decode(BitReader& br, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
        uint32_t bit = 0;
        if (!br.Get(1, bit)) return -1;
        code |= static_cast<int>(bit);
        int count = h.counts[len];
        if (code - first < count) return h.symbols[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

// 输出：32KB 环形窗口（给回填用）+ 32KB 写块缓冲（给落盘用）
class Output {
public:
    Output(ByteSink& sink) : sink_(sink) {}

    bool Put(uint8_t b) {
        win_[wpos_] = b;
        wpos_ = (wpos_ + 1) & 32767;
        block_[bpos_++] = b;
        if (bpos_ == sizeof block_) return Flush();
        return true;
    }

    bool CopyBack(int dist, int len) {
        for (int i = 0; i < len; ++i) {
            uint8_t b = win_[(wpos_ + 32768 - dist) & 32767];
            if (!Put(b)) return false;
        }
        return true;
    }

    bool Finish() { return Flush(); }

private:
    bool Flush() {
        if (!bpos_) return true;
        bool ok = sink_.Write(block_, bpos_);
        bpos_ = 0;
        return ok;
    }

    ByteSink& sink_;
    uint8_t win_[32768] = {};
    uint32_t wpos_ = 0;
    uint8_t block_[32768];
    size_t bpos_ = 0;
};

bool ReadStored(BitReader& br, Output& out, uint32_t hdrBitsErr) {
    br.AlignByte();
    uint32_t len = 0, nlen = 0;
    if (!br.Get(16, len) || !br.Get(16, nlen)) return false;
    if ((len & 0xFFFF) != ((~nlen) & 0xFFFF)) return false;
    for (uint32_t i = 0; i < len; ++i) {
        uint32_t b = 0;
        if (!br.Get(8, b)) return false;
        if (!out.Put(static_cast<uint8_t>(b))) return false;
    }
    return true;
}

bool ReadHuffman(BitReader& br, Output& out, const Huffman& lit, const Huffman& dist) {
    while (true) {
        int sym = Decode(br, lit);
        if (sym < 0) return false;
        if (sym < 256) {
            if (!out.Put(static_cast<uint8_t>(sym))) return false;
        } else if (sym == 256) {
            return true;
        } else {
            int li = sym - 257;
            if (li >= 29) return false;
            int len = kLenBase[li] + static_cast<int>(br.GetUnchecked(kLenExtra[li]));
            int dsym = Decode(br, dist);
            if (dsym < 0 || dsym >= 30) return false;
            int d = kDistBase[dsym] + static_cast<int>(br.GetUnchecked(kDistExtra[dsym]));
            if (!out.CopyBack(d, len)) return false;
        }
    }
}

// 固定 Huffman：0-143 用 8 位，144-255 用 9 位，256-279 用 7 位，280-287 用 8 位
void FilledFixedLits(uint8_t lens[288]) {
    for (int i = 0; i < 144; ++i) lens[i] = 8;
    for (int i = 144; i < 256; ++i) lens[i] = 9;
    for (int i = 256; i < 280; ++i) lens[i] = 7;
    for (int i = 280; i < 288; ++i) lens[i] = 8;
}

bool ReadDynamic(BitReader& br, Output& out, std::string& err) {
    int hlit = static_cast<int>(br.GetUnchecked(5)) + 257;
    int hdist = static_cast<int>(br.GetUnchecked(5)) + 1;
    int hclen = static_cast<int>(br.GetUnchecked(4)) + 4;

    static const uint8_t kOrder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint8_t clLens[19] = {};
    for (int i = 0; i < hclen; ++i) clLens[kOrder[i]] = static_cast<uint8_t>(br.GetUnchecked(3));
    Huffman cl;
    if (Build(cl, clLens, 19) < 0) {
        err = "invalid code-length huffman table";
        return false;
    }

    std::vector<uint8_t> lens(static_cast<size_t>(hlit + hdist), 0);
    int n = 0;
    while (n < hlit + hdist) {
        int sym = Decode(br, cl);
        if (sym < 0) {
            err = "invalid code-length symbol";
            return false;
        }
        if (sym < 16) {
            lens[n++] = static_cast<uint8_t>(sym);
            continue;
        }
        int rep = 0;
        uint8_t val = 0;
        if (sym == 16) {
            if (n == 0) {
                err = "repeat with no previous length";
                return false;
            }
            val = lens[n - 1];
            rep = 3 + static_cast<int>(br.GetUnchecked(2));
        } else if (sym == 17) {
            rep = 3 + static_cast<int>(br.GetUnchecked(3));
        } else {
            rep = 11 + static_cast<int>(br.GetUnchecked(7));
        }
        if (n + rep > hlit + hdist) {
            err = "code-length repeat overrun";
            return false;
        }
        for (int i = 0; i < rep; ++i) lens[n++] = val;
    }

    if (lens[256] == 0) {
        err = "missing end-of-block code";
        return false;
    }

    Huffman lit, dist;
    int litLeft = Build(lit, lens.data(), hlit);
    int distLeft = Build(dist, lens.data() + hlit, hdist);
    if (litLeft < 0 || distLeft < 0) {
        err = "invalid literal/distance huffman table";
        return false;
    }
    return ReadHuffman(br, out, lit, dist);
}

} // namespace

bool Inflate(ByteSource& src, ByteSink& sink, std::string& err) {
    BitReader br(src);
    Output out(sink);

    while (true) {
        uint32_t last = 0, type = 0;
        if (!br.Get(1, last) || !br.Get(2, type)) {
            err = "truncated deflate header";
            return false;
        }
        bool ok = false;
        switch (type) {
            case 0:
                ok = ReadStored(br, out, 0);
                if (!ok) err = "truncated stored block";
                break;
            case 1: {
                Huffman lit, dist;
                uint8_t ll[288];
                FilledFixedLits(ll);
                if (Build(lit, ll, 288) < 0) {
                    err = "invalid fixed table";
                    return false;
                }
                uint8_t dl[30];
                memset(dl, 5, sizeof dl);
                if (Build(dist, dl, 30) < 0) {
                    err = "invalid fixed distance table";
                    return false;
                }
                ok = ReadHuffman(br, out, lit, dist);
                if (!ok && err.empty()) err = "corrupt huffman block";
                break;
            }
            case 2:
                ok = ReadDynamic(br, out, err);
                break;
            default:
                err = "invalid block type";
                return false;
        }
        if (!ok) {
            if (err.empty()) err = "inflate failed";
            return false;
        }
        if (last) break;
    }
    if (!out.Finish()) {
        err = "write failed";
        return false;
    }
    return true;
}

} // namespace detail
} // namespace xgzip
