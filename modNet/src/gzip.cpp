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

// 自带 gzip 解码（不链 zlib）。deflate 部分是与 XGZIP 同一套逐位规范 Huffman 解码，
// 这里单独放一份，保持 modNet 零第三方依赖。

#include "gzip.h"

#include <cstring>
#include <vector>

namespace xgnet {
namespace detail {

namespace {

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                               3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193,
                                12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct Huffman {
    uint16_t counts[16];
    uint16_t symbols[288];
    void Clear() { memset(counts, 0, sizeof counts); }
};

// 返回未配平码数（0 = 完整），负数 = 超配（非法）
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
    for (int len = 1; len < 15; ++len) offs[len + 1] = (uint16_t)(offs[len] + h.counts[len]);
    for (int i = 0; i < n; ++i)
        if (lens[i]) h.symbols[offs[lens[i]]++] = (uint16_t)i;
    return left;
}

class BufSource {
public:
    BufSource(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool Read(uint8_t* dst, size_t want, size_t& got) {
        got = 0;
        if (pos_ >= n_) return true;
        size_t take = n_ - pos_;
        if (take > want) take = want;
        memcpy(dst, p_ + pos_, take);
        pos_ += take;
        got = take;
        return true;
    }

private:
    const uint8_t* p_;
    size_t n_, pos_ = 0;
};

class BitReader {
public:
    explicit BitReader(BufSource& src) : src_(src) {}

    bool Get(int n, uint32_t& out) {
        while (bits_ < n) {
            if (cpos_ == cend_ && !Refill()) return false;
            acc_ |= (uint32_t)chunk_[cpos_++] << bits_;
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

    void AlignByte() {
        acc_ = 0;
        bits_ = 0;
    }

private:
    bool Refill() {
        size_t got = 0;
        if (!src_.Read(chunk_, sizeof chunk_, got) || got == 0) return false;
        cpos_ = 0;
        cend_ = got;
        return true;
    }

    BufSource& src_;
    uint8_t chunk_[4096];
    size_t cpos_ = 0, cend_ = 0;
    uint32_t acc_ = 0;
    int bits_ = 0;
};

int Decode(BitReader& br, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
        uint32_t bit = 0;
        if (!br.Get(1, bit)) return -1;
        code |= (int)bit;
        int count = h.counts[len];
        if (code - first < count) return h.symbols[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

class Output {
public:
    explicit Output(std::string& out) : out_(out) {}

    bool Put(uint8_t b) {
        win_[wpos_] = b;
        wpos_ = (wpos_ + 1) & 32767;
        out_.push_back((char)b);
        return true;
    }

    bool CopyBack(int dist, int len) {
        for (int i = 0; i < len; ++i) Put(win_[(wpos_ + 32768 - dist) & 32767]);
        return true;
    }

private:
    std::string& out_;
    uint8_t win_[32768] = {};
    uint32_t wpos_ = 0;
};

bool ReadStored(BitReader& br, Output& out) {
    br.AlignByte();
    uint32_t len = 0, nlen = 0;
    if (!br.Get(16, len) || !br.Get(16, nlen)) return false;
    if ((len & 0xFFFF) != ((~nlen) & 0xFFFF)) return false;
    for (uint32_t i = 0; i < len; ++i) {
        uint32_t b = 0;
        if (!br.Get(8, b)) return false;
        out.Put((uint8_t)b);
    }
    return true;
}

bool ReadHuffman(BitReader& br, Output& out, const Huffman& lit, const Huffman& dist) {
    while (true) {
        int sym = Decode(br, lit);
        if (sym < 0) return false;
        if (sym < 256) {
            out.Put((uint8_t)sym);
        } else if (sym == 256) {
            return true;
        } else {
            int li = sym - 257;
            if (li >= 29) return false;
            int len = kLenBase[li] + (int)br.GetUnchecked(kLenExtra[li]);
            int dsym = Decode(br, dist);
            if (dsym < 0 || dsym >= 30) return false;
            int d = kDistBase[dsym] + (int)br.GetUnchecked(kDistExtra[dsym]);
            out.CopyBack(d, len);
        }
    }
}

void FixedLits(uint8_t lens[288]) {
    for (int i = 0; i < 144; ++i) lens[i] = 8;
    for (int i = 144; i < 256; ++i) lens[i] = 9;
    for (int i = 256; i < 280; ++i) lens[i] = 7;
    for (int i = 280; i < 288; ++i) lens[i] = 8;
}

bool ReadDynamic(BitReader& br, Output& out, std::string& err) {
    int hlit = (int)br.GetUnchecked(5) + 257;
    int hdist = (int)br.GetUnchecked(5) + 1;
    int hclen = (int)br.GetUnchecked(4) + 4;
    static const uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                                       11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint8_t clLens[19] = {};
    for (int i = 0; i < hclen; ++i) clLens[kOrder[i]] = (uint8_t)br.GetUnchecked(3);
    Huffman cl;
    if (Build(cl, clLens, 19) < 0) { err = "invalid code-length table"; return false; }

    std::vector<uint8_t> lens((size_t)(hlit + hdist), 0);
    int n = 0;
    while (n < hlit + hdist) {
        int sym = Decode(br, cl);
        if (sym < 0) { err = "invalid code-length symbol"; return false; }
        if (sym < 16) { lens[n++] = (uint8_t)sym; continue; }
        int rep = 0;
        uint8_t val = 0;
        if (sym == 16) {
            if (n == 0) { err = "repeat with no previous length"; return false; }
            val = lens[n - 1];
            rep = 3 + (int)br.GetUnchecked(2);
        } else if (sym == 17) {
            rep = 3 + (int)br.GetUnchecked(3);
        } else {
            rep = 11 + (int)br.GetUnchecked(7);
        }
        if (n + rep > hlit + hdist) { err = "code-length repeat overrun"; return false; }
        for (int i = 0; i < rep; ++i) lens[n++] = val;
    }
    if (lens[256] == 0) { err = "missing end-of-block code"; return false; }

    Huffman lit, dist;
    if (Build(lit, lens.data(), hlit) < 0 || Build(dist, lens.data() + hlit, hdist) < 0) {
        err = "invalid huffman table";
        return false;
    }
    return ReadHuffman(br, out, lit, dist);
}

bool InflateRaw(const uint8_t* data, size_t len, std::string& out, std::string& err) {
    BufSource src(data, len);
    BitReader br(src);
    Output sink(out);
    while (true) {
        uint32_t last = 0, type = 0;
        if (!br.Get(1, last) || !br.Get(2, type)) { err = "truncated deflate header"; return false; }
        bool ok = false;
        switch (type) {
        case 0: ok = ReadStored(br, sink); if (!ok) err = "truncated stored block"; break;
        case 1: {
            Huffman lit, dist;
            uint8_t ll[288];
            FixedLits(ll);
            if (Build(lit, ll, 288) < 0) { err = "invalid fixed table"; return false; }
            uint8_t dl[30];
            memset(dl, 5, sizeof dl);
            if (Build(dist, dl, 30) < 0) { err = "invalid fixed distance table"; return false; }
            ok = ReadHuffman(br, sink, lit, dist);
            if (!ok && err.empty()) err = "corrupt huffman block";
            break;
        }
        case 2: ok = ReadDynamic(br, sink, err); break;
        default: err = "invalid block type"; return false;
        }
        if (!ok) {
            if (err.empty()) err = "inflate failed";
            return false;
        }
        if (last) break;
    }
    return true;
}

uint32_t Crc32(const uint8_t* data, size_t len, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

uint32_t Rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

} // namespace

bool Gunzip(const std::string& in, std::string& out, std::string& err) {
    out.clear();
    const uint8_t* p = reinterpret_cast<const uint8_t*>(in.data());
    size_t n = in.size();
    if (n < 18) { err = "gzip stream too short"; return false; }
    if (p[0] != 0x1F || p[1] != 0x8B) { err = "bad gzip magic"; return false; }
    if (p[2] != 8) { err = "unsupported gzip method"; return false; }

    uint8_t flags = p[3];
    size_t pos = 10;
    if (flags & 0x04) {   // FEXTRA
        if (pos + 2 > n) { err = "truncated gzip extra"; return false; }
        uint16_t xlen = (uint16_t)(p[pos] | (p[pos + 1] << 8));
        pos += 2 + xlen;
    }
    if (flags & 0x08) {   // FNAME
        while (pos < n && p[pos]) ++pos;
        ++pos;
    }
    if (flags & 0x10) {   // FCOMMENT
        while (pos < n && p[pos]) ++pos;
        ++pos;
    }
    if (flags & 0x02) pos += 2;   // FHCRC
    if (pos + 8 > n) { err = "truncated gzip trailer"; return false; }

    size_t dataLen = n - pos - 8;
    if (!InflateRaw(p + pos, dataLen, out, err)) return false;

    uint32_t wantCrc = Rd32(p + n - 8);
    uint32_t wantSize = Rd32(p + n - 4);
    uint32_t gotCrc = Crc32(reinterpret_cast<const uint8_t*>(out.data()), out.size());
    if (gotCrc != wantCrc) { err = "gzip crc mismatch"; out.clear(); return false; }
    if (wantSize != (uint32_t)(out.size() & 0xFFFFFFFF)) {
        err = "gzip size mismatch";
        out.clear();
        return false;
    }
    return true;
}

} // namespace detail
} // namespace xgnet
