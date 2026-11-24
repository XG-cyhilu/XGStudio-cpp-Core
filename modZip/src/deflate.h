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
#include <vector>

namespace xgzip {
namespace detail {

// 固定 Huffman 的 deflate 压缩（单块，BFINAL=1 / BTYPE=01）。
// 只做 LZ77 + 固定码表，不生成动态码表：解压侧（包括本库 inflate 和系统 zip）都认。
// effort：1 最快（短链），9 最慢（长链搜索）。
bool DeflateFixed(const uint8_t* data, size_t len, std::vector<uint8_t>& out, int effort = 6);

} // namespace detail
} // namespace xgzip
