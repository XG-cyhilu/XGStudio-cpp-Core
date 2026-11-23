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
#include <vector>

// XGCFG —— JSON 值 + 配置文件（Windows / C++17 / 静态库 / Apache-2.0）
//
// 两件事：
//   1) 一个够用的 JSON 值树（parse / dump），解析时容忍注释与尾逗号；
//   2) 一个配置文件封装：点路径访问、默认值、原子保存、按 mtime 变化重载。
// 不做：JSON Schema 校验、YAML/TOML、二进制序列化。

namespace xgcfg {

class Value {
public:
    enum class Type { Null, Bool, Int, Real, String, Array, Object };

    Value();
    Value(bool v);
    Value(int v);
    Value(int64_t v);
    Value(uint32_t v);
    Value(double v);
    Value(const char* v);
    Value(std::string v);

    Type GetType() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsInt() const { return type_ == Type::Int; }
    bool IsReal() const { return type_ == Type::Real; }
    bool IsNumber() const { return type_ == Type::Int || type_ == Type::Real; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    bool AsBool(bool def = false) const;
    int64_t AsInt(int64_t def = 0) const;
    double AsDouble(double def = 0.0) const;
    std::string AsString(const std::string& def = std::string()) const;

    // 数组
    size_t Size() const;
    Value& At(size_t i);
    const Value& At(size_t i) const;
    Value& Push(Value v);
    bool RemoveAt(size_t i);

    // 对象（保持插入顺序，便于 diff 与手改）
    bool Has(const std::string& key) const;
    Value* Find(const std::string& key);
    const Value* Find(const std::string& key) const;
    Value& operator[](const std::string& key);          // 不存在则创建
    const Value& operator[](const std::string& key) const;
    bool Remove(const std::string& key);
    std::vector<std::string> Keys() const;

    // 点路径：a.b[0].c
    Value* FindPath(const std::string& path);
    const Value* FindPath(const std::string& path) const;
    bool SetPath(const std::string& path, Value v);      // 中间对象自动建
    bool RemovePath(const std::string& path);

    // 给解析器/序列化用的容器操作（保持顺序）
    void EnsureObject();
    void EnsureArray();
    void SetKey(std::string key, Value v);

private:
    Type type_ = Type::Null;
    bool b_ = false;
    int64_t i_ = 0;
    double d_ = 0.0;
    std::string s_;
    std::vector<Value> arr_;
    std::vector<std::pair<std::string, Value>> obj_;
};

// 解析 JSON；allowLoose 打开时允许 // /* */ 注释与尾逗号。err 带字节偏移。
bool Parse(const std::string& text, Value& out, std::string* err = nullptr, bool allowLoose = true);

// 序列化；pretty=false 时输出紧凑格式
std::string Dump(const Value& v, bool pretty = true, int indent = 2);

class Config {
public:
    Config() = default;

    bool Load(const std::wstring& path, std::string* err = nullptr);
    bool LoadText(const std::string& text, std::string* err = nullptr);
    bool Save(std::string* err = nullptr, bool pretty = true);          // 原子写
    bool SaveAs(const std::wstring& path, bool pretty = true, std::string* err = nullptr);

    const std::wstring& Path() const { return path_; }
    Value& Root() { return root_; }
    const Value& Root() const { return root_; }

    // 文件被外部改过（按 mtime + 大小判断，不读内容）
    bool Changed() const;
    bool Reload(std::string* err = nullptr);

    bool Has(const std::string& path) const;
    bool Remove(const std::string& path);
    void Clear() { root_ = Value(); }

    bool GetBool(const std::string& path, bool def = false) const;
    int64_t GetInt(const std::string& path, int64_t def = 0) const;
    double GetDouble(const std::string& path, double def = 0.0) const;
    std::string GetString(const std::string& path, const std::string& def = std::string()) const;
    bool Set(const std::string& path, const Value& v);

private:
    std::wstring path_;
    Value root_;
    int64_t mtimeMs_ = 0;
    uint64_t size_ = 0;
};

} // namespace xgcfg
