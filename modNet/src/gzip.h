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

#include <cstdint>
#include <string>

namespace xgnet {
namespace detail {

// 解开一个完整 gzip 流（含头尾），校验 CRC32 与 ISIZE。
bool Gunzip(const std::string& in, std::string& out, std::string& err);

} // namespace detail
} // namespace xgnet
