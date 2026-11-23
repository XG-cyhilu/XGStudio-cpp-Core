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

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace xgzip {
namespace detail {

// 压缩数据源：按需喂字节，读到 EOF 时 got == 0
class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual bool Read(uint8_t* dst, size_t want, size_t& got) = 0;
};

// 解压输出：所有解出来的字节都走这里（窗口回填也在内部完成）
class ByteSink {
public:
    virtual ~ByteSink() = default;
    virtual bool Write(const uint8_t* src, size_t len) = 0;
};

// 解一个原始 deflate 流（无 zlib 头、无 gzip 头）
bool Inflate(ByteSource& src, ByteSink& sink, std::string& err);

} // namespace detail
} // namespace xgzip
