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
#include <functional>
#include <memory>
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
    void Clear() { root_ = Value(); dirty_ = true; }

    // 自上次 Load / Save 以来有没有改过。Set / Remove / Clear 会置位，Save 会清零。
    bool Dirty() const { return dirty_; }

    // 变更回调：Set / Remove 成功后触发（path 是点路径；删除时 newValue 为 null）。
    // 回调里不要再改同一个 Config（会递归触发），也不要在回调里 Save（互相踩）。
    void SetOnChange(std::function<void(const std::string& path, const Value& v)> cb);

    // 深合并另一份配置：对象递归合并，数组与标量整体替换。
    // overwriteExisting=false 时只补缺失键，已有值保持不动（适合"默认值 + 用户覆盖"）。
    // 返回实际发生变化的位置数量。
    size_t Merge(const Config& other, bool overwriteExisting = true);
    size_t Merge(const Value& other, bool overwriteExisting = true);

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
    bool dirty_ = false;
    std::function<void(const std::string&, const Value&)> onChange_;
};

// ---------------- 结构体绑定 ----------------
//
// 不用反射：调用方把结构体字段逐个 Bind 进来，然后 Pull（配置 → 变量）或 Push（变量 → 配置）。
// 没绑定前 Pull 时配置里缺键不会改变量（沿用变量现有值当默认），Push 时会把变量写进配置。
//
//   struct Settings { bool tray = true; int64_t threads = 4; std::string theme = "dark"; };
//   Settings s;
//   xgcfg::Binder b(cfg, "ui");
//   b.BindBool("tray", s.tray).BindInt("threads", s.threads).BindString("theme", s.theme);
//   b.Pull();
class Binder {
public:
    explicit Binder(Config& cfg, const std::string& prefix = std::string());
    ~Binder();
    Binder(const Binder&) = delete;
    Binder& operator=(const Binder&) = delete;

    Binder& BindBool(const std::string& key, bool& ref);
    Binder& BindInt(const std::string& key, int64_t& ref);
    Binder& BindInt(const std::string& key, int& ref);
    Binder& BindInt(const std::string& key, uint32_t& ref);
    Binder& BindDouble(const std::string& key, double& ref);
    Binder& BindString(const std::string& key, std::string& ref);

    // 配置 → 变量；返回实际被更新的字段数
    size_t Pull();
    // 变量 → 配置；返回实际发生变化的字段数
    size_t Push();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xgcfg
