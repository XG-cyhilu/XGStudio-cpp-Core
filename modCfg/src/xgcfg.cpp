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

#include "xgcfg/xgcfg.h"

#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace xgcfg {
namespace {

int64_t FileTimeToMs(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    if (!u.QuadPart) return 0;
    return static_cast<int64_t>((u.QuadPart - 116444736000000000ull) / 10000ull);
}

bool Stat(const std::wstring& path, int64_t& ms, uint64_t& size) {
    WIN32_FILE_ATTRIBUTE_DATA ad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &ad)) return false;
    if (ad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    ms = FileTimeToMs(ad.ftLastWriteTime);
    size = (static_cast<uint64_t>(ad.nFileSizeHigh) << 32) | ad.nFileSizeLow;
    return true;
}

bool ReadAll(const std::wstring& path, std::string& out, std::string* err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) *err = "open failed";
        return false;
    }
    out.clear();
    char buf[64 * 1024];
    while (true) {
        DWORD got = 0;
        if (!::ReadFile(h, buf, sizeof buf, &got, nullptr)) {
            CloseHandle(h);
            if (err) *err = "read failed";
            return false;
        }
        if (!got) break;
        out.append(buf, got);
    }
    CloseHandle(h);
    return true;
}

bool WriteAtomic(const std::wstring& path, const std::string& text, std::string* err) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) *err = "create temp failed";
        return false;
    }
    size_t done = 0;
    bool ok = true;
    while (done < text.size()) {
        DWORD want = static_cast<DWORD>(std::min<size_t>(text.size() - done, 1u << 20));
        DWORD wrote = 0;
        if (!::WriteFile(h, text.data() + done, want, &wrote, nullptr) || wrote != want) {
            ok = false;
            break;
        }
        done += wrote;
    }
    if (ok) FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        if (err) *err = "write temp failed";
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        if (err) *err = "replace failed";
        return false;
    }
    return true;
}

std::string Shortest(double v) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, v);
    std::string s(buf, r.ptr);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
        s.find("inf") == std::string::npos && s.find("nan") == std::string::npos)
        s += ".0";
    return s;
}

// ---------- 路径段解析 ----------

struct Seg {
    bool isIndex = false;
    std::string key;
    size_t index = 0;
};

bool ParsePath(const std::string& path, std::vector<Seg>& segs) {
    segs.clear();
    size_t i = 0;
    while (i < path.size()) {
        if (path[i] == '.') {
            if (segs.empty() || (!segs.back().isIndex && segs.back().key.empty())) return false;
            ++i;
            continue;
        }
        if (path[i] == '[') {
            size_t close = path.find(']', i);
            if (close == std::string::npos || close == i + 1) return false;
            std::string num = path.substr(i + 1, close - i - 1);
            for (char c : num)
                if (c < '0' || c > '9') return false;
            Seg s;
            s.isIndex = true;
            s.index = static_cast<size_t>(std::strtoull(num.c_str(), nullptr, 10));
            segs.push_back(s);
            i = close + 1;
            if (i < path.size() && path[i] != '.' && path[i] != '[') return false;
            continue;
        }
        size_t end = path.find_first_of(".[", i);
        if (end == std::string::npos) end = path.size();
        Seg s;
        s.key = path.substr(i, end - i);
        if (s.key.empty()) return false;
        segs.push_back(s);
        i = end;
    }
    return !segs.empty();
}

// ---------- JSON 解析 ----------

class Parser {
public:
    Parser(const std::string& s, bool loose) : s_(s), loose_(loose) {}

    bool Run(Value& out, std::string* err) {
        SkipWs();
        if (!ParseValue(out, 0)) return Fail(err);
        SkipWs();
        if (p_ != s_.size()) {
            errPos_ = p_;
            msg_ = "trailing characters";
            return Fail(err);
        }
        return true;
    }

private:
    void SkipWs() {
        while (p_ < s_.size()) {
            char c = s_[p_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++p_;
                continue;
            }
            if (!loose_ || c != '/') break;
            if (p_ + 1 >= s_.size()) break;
            if (s_[p_ + 1] == '/') {
                p_ += 2;
                while (p_ < s_.size() && s_[p_] != '\n') ++p_;
                continue;
            }
            if (s_[p_ + 1] == '*') {
                size_t end = s_.find("*/", p_ + 2);
                if (end == std::string::npos) {
                    p_ = s_.size();
                    break;
                }
                p_ = end + 2;
                continue;
            }
            break;
        }
    }

    bool Fail(std::string* err) {
        if (err) {
            char buf[160];
            _snprintf_s(buf, sizeof buf, _TRUNCATE, "json parse error at byte %zu: %s", errPos_, msg_.c_str());
            *err = buf;
        }
        return false;
    }

    bool ParseValue(Value& out, int depth) {
        if (depth > 500) {
            errPos_ = p_;
            msg_ = "nesting too deep";
            return false;
        }
        if (p_ >= s_.size()) {
            errPos_ = p_;
            msg_ = "unexpected end of input";
            return false;
        }
        char c = s_[p_];
        switch (c) {
            case '{': return ParseObject(out, depth);
            case '[': return ParseArray(out, depth);
            case '"': {
                std::string str;
                if (!ParseString(str)) return false;
                out = Value(std::move(str));
                return true;
            }
            case 't':
                if (s_.compare(p_, 4, "true") == 0) {
                    p_ += 4;
                    out = Value(true);
                    return true;
                }
                break;
            case 'f':
                if (s_.compare(p_, 5, "false") == 0) {
                    p_ += 5;
                    out = Value(false);
                    return true;
                }
                break;
            case 'n':
                if (s_.compare(p_, 4, "null") == 0) {
                    p_ += 4;
                    out = Value();
                    return true;
                }
                break;
            default: break;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
        errPos_ = p_;
        msg_ = "unexpected character";
        return false;
    }

    bool ParseObject(Value& out, int depth) {
        ++p_;   // {
        out = Value();
        out.EnsureObject();
        SkipWs();
        if (p_ < s_.size() && s_[p_] == '}') {
            ++p_;
            return true;
        }
        while (true) {
            SkipWs();
            if (p_ >= s_.size() || s_[p_] != '"') {
                errPos_ = p_;
                msg_ = "expected object key";
                return false;
            }
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (p_ >= s_.size() || s_[p_] != ':') {
                errPos_ = p_;
                msg_ = "expected ':'";
                return false;
            }
            ++p_;
            SkipWs();
            Value v;
            if (!ParseValue(v, depth + 1)) return false;
            out.SetKey(std::move(key), std::move(v));
            SkipWs();
            if (p_ < s_.size() && s_[p_] == ',') {
                ++p_;
                SkipWs();
                if (loose_ && p_ < s_.size() && s_[p_] == '}') {   // 尾逗号
                    ++p_;
                    return true;
                }
                continue;
            }
            if (p_ < s_.size() && s_[p_] == '}') {
                ++p_;
                return true;
            }
            errPos_ = p_;
            msg_ = "expected ',' or '}'";
            return false;
        }
    }

    bool ParseArray(Value& out, int depth) {
        ++p_;   // [
        out = Value();
        out.EnsureArray();
        SkipWs();
        if (p_ < s_.size() && s_[p_] == ']') {
            ++p_;
            return true;
        }
        while (true) {
            SkipWs();
            Value v;
            if (!ParseValue(v, depth + 1)) return false;
            out.Push(std::move(v));
            SkipWs();
            if (p_ < s_.size() && s_[p_] == ',') {
                ++p_;
                SkipWs();
                if (loose_ && p_ < s_.size() && s_[p_] == ']') {   // 尾逗号
                    ++p_;
                    return true;
                }
                continue;
            }
            if (p_ < s_.size() && s_[p_] == ']') {
                ++p_;
                return true;
            }
            errPos_ = p_;
            msg_ = "expected ',' or ']'";
            return false;
        }
    }

    bool ParseString(std::string& out) {
        ++p_;   // "
        out.clear();
        while (true) {
            if (p_ >= s_.size()) {
                errPos_ = p_;
                msg_ = "unterminated string";
                return false;
            }
            unsigned char c = static_cast<unsigned char>(s_[p_]);
            if (c == '"') {
                ++p_;
                return true;
            }
            if (c == '\\') {
                ++p_;
                if (p_ >= s_.size()) {
                    errPos_ = p_;
                    msg_ = "unterminated escape";
                    return false;
                }
                char e = s_[p_++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!Hex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // 代理对
                            if (p_ + 1 < s_.size() && s_[p_] == '\\' && s_[p_ + 1] == 'u') {
                                p_ += 2;
                                uint32_t lo = 0;
                                if (!Hex4(lo)) return false;
                                if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            }
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default:
                        errPos_ = p_ - 1;
                        msg_ = "invalid escape";
                        return false;
                }
                continue;
            }
            if (c < 0x20) {
                errPos_ = p_;
                msg_ = "raw control character in string";
                return false;
            }
            out += static_cast<char>(c);
            ++p_;
        }
    }

    bool Hex4(uint32_t& v) {
        if (p_ + 4 > s_.size()) {
            errPos_ = p_;
            msg_ = "truncated \\u escape";
            return false;
        }
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[p_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else {
                errPos_ = p_ - 1;
                msg_ = "invalid \\u escape";
                return false;
            }
        }
        return true;
    }

    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool ParseNumber(Value& out) {
        size_t start = p_;
        if (p_ < s_.size() && s_[p_] == '-') ++p_;
        if (p_ >= s_.size()) {
            errPos_ = p_;
            msg_ = "truncated number";
            return false;
        }
        if (s_[p_] == '0') {
            ++p_;
        } else if (s_[p_] >= '1' && s_[p_] <= '9') {
            while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
        } else {
            errPos_ = p_;
            msg_ = "invalid number";
            return false;
        }
        bool isReal = false;
        if (p_ < s_.size() && s_[p_] == '.') {
            isReal = true;
            ++p_;
            size_t digits = p_;
            while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
            if (p_ == digits) {
                errPos_ = p_;
                msg_ = "digits expected after '.'";
                return false;
            }
        }
        if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) {
            isReal = true;
            ++p_;
            if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-')) ++p_;
            size_t digits = p_;
            while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
            if (p_ == digits) {
                errPos_ = p_;
                msg_ = "exponent digits expected";
                return false;
            }
        }
        std::string tok = s_.substr(start, p_ - start);
        if (!isReal) {
            errno = 0;
            char* end = nullptr;
            long long v = std::strtoll(tok.c_str(), &end, 10);
            if (errno == 0 && end && *end == 0) {
                out = Value(static_cast<int64_t>(v));
                return true;
            }
        }
        out = Value(std::strtod(tok.c_str(), nullptr));
        return true;
    }

    const std::string& s_;
    bool loose_;
    size_t p_ = 0;
    size_t errPos_ = 0;
    std::string msg_;
};

// ---------- 序列化 ----------

void EscapeTo(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    _snprintf_s(buf, sizeof buf, _TRUNCATE, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void DumpTo(const Value& v, std::string& out, bool pretty, int indent, int level) {
    auto newline = [&](int lvl) {
        if (!pretty) return;
        out += '\n';
        out.append(static_cast<size_t>(indent * lvl), ' ');
    };

    switch (v.GetType()) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.AsBool() ? "true" : "false"; break;
        case Value::Type::Int: out += std::to_string(v.AsInt()); break;
        case Value::Type::Real:
            if (std::isfinite(v.AsDouble())) out += Shortest(v.AsDouble());
            else out += "null";
            break;
        case Value::Type::String: EscapeTo(out, v.AsString()); break;
        case Value::Type::Array: {
            out += '[';
            size_t n = v.Size();
            for (size_t i = 0; i < n; ++i) {
                if (i) out += ',';
                if (pretty) out += ' ';
                DumpTo(v.At(i), out, pretty, indent, level + 1);
            }
            if (pretty && n) out += ' ';
            out += ']';
            break;
        }
        case Value::Type::Object: {
            out += '{';
            std::vector<std::string> keys = v.Keys();
            for (size_t i = 0; i < keys.size(); ++i) {
                if (i) out += ',';
                newline(level + 1);
                EscapeTo(out, keys[i]);
                out += pretty ? ": " : ":";
                DumpTo(v[keys[i]], out, pretty, indent, level + 1);
            }
            if (!keys.empty()) newline(level);
            out += '}';
            break;
        }
    }
}

} // namespace

// ---------- Value ----------

Value::Value() = default;
Value::Value(bool v) : type_(Type::Bool), b_(v) {}
Value::Value(int v) : type_(Type::Int), i_(v) {}
Value::Value(int64_t v) : type_(Type::Int), i_(v) {}
Value::Value(uint32_t v) : type_(Type::Int), i_(static_cast<int64_t>(v)) {}
Value::Value(double v) : type_(Type::Real), d_(v) {}
Value::Value(const char* v) : type_(Type::String), s_(v ? v : "") {}
Value::Value(std::string v) : type_(Type::String), s_(std::move(v)) {}

bool Value::AsBool(bool def) const {
    if (type_ == Type::Bool) return b_;
    if (type_ == Type::Int) return i_ != 0;
    return def;
}

int64_t Value::AsInt(int64_t def) const {
    if (type_ == Type::Int) return i_;
    if (type_ == Type::Real) {
        if (!std::isfinite(d_)) return def;
        if (d_ >= 9.2e18 || d_ <= -9.2e18) return def;
        return static_cast<int64_t>(d_);
    }
    if (type_ == Type::Bool) return b_ ? 1 : 0;
    return def;
}

double Value::AsDouble(double def) const {
    if (type_ == Type::Real) return d_;
    if (type_ == Type::Int) return static_cast<double>(i_);
    if (type_ == Type::Bool) return b_ ? 1.0 : 0.0;
    return def;
}

std::string Value::AsString(const std::string& def) const {
    if (type_ == Type::String) return s_;
    if (type_ == Type::Bool) return b_ ? "true" : "false";
    if (type_ == Type::Int) return std::to_string(i_);
    if (type_ == Type::Real) return Shortest(d_);
    return def;
}

size_t Value::Size() const {
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    return 0;
}

Value& Value::At(size_t i) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        arr_.clear();
    }
    if (i >= arr_.size()) arr_.resize(i + 1);
    return arr_[i];
}

const Value& Value::At(size_t i) const {
    static const Value kNull;
    if (type_ != Type::Array || i >= arr_.size()) return kNull;
    return arr_[i];
}

Value& Value::Push(Value v) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        arr_.clear();
    }
    arr_.push_back(std::move(v));
    return arr_.back();
}

bool Value::RemoveAt(size_t i) {
    if (type_ != Type::Array || i >= arr_.size()) return false;
    arr_.erase(arr_.begin() + static_cast<long long>(i));
    return true;
}

bool Value::Has(const std::string& key) const { return Find(key) != nullptr; }

void Value::EnsureObject() {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        obj_.clear();
        arr_.clear();
        s_.clear();
    }
}

void Value::EnsureArray() {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        arr_.clear();
        obj_.clear();
        s_.clear();
    }
}

void Value::SetKey(std::string key, Value v) {
    EnsureObject();
    for (auto& kv : obj_) {
        if (kv.first == key) {
            kv.second = std::move(v);
            return;
        }
    }
    obj_.emplace_back(std::move(key), std::move(v));
}

Value* Value::Find(const std::string& key) {
    if (type_ != Type::Object) return nullptr;
    for (auto& kv : obj_)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

const Value* Value::Find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& kv : obj_)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

Value& Value::operator[](const std::string& key) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        obj_.clear();
    }
    for (auto& kv : obj_)
        if (kv.first == key) return kv.second;
    obj_.emplace_back(key, Value());
    return obj_.back().second;
}

const Value& Value::operator[](const std::string& key) const {
    static const Value kNull;
    const Value* v = Find(key);
    return v ? *v : kNull;
}

bool Value::Remove(const std::string& key) {
    if (type_ != Type::Object) return false;
    for (auto it = obj_.begin(); it != obj_.end(); ++it) {
        if (it->first == key) {
            obj_.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<std::string> Value::Keys() const {
    std::vector<std::string> ks;
    if (type_ != Type::Object) return ks;
    ks.reserve(obj_.size());
    for (const auto& kv : obj_) ks.push_back(kv.first);
    return ks;
}

Value* Value::FindPath(const std::string& path) {
    std::vector<Seg> segs;
    if (!ParsePath(path, segs)) return nullptr;
    Value* cur = this;
    for (const Seg& s : segs) {
        if (s.isIndex) {
            if (cur->GetType() != Type::Array || s.index >= cur->arr_.size()) return nullptr;
            cur = &cur->arr_[s.index];
        } else {
            if (cur->GetType() != Type::Object) return nullptr;
            Value* next = cur->Find(s.key);
            if (!next) return nullptr;
            cur = next;
        }
    }
    return cur;
}

const Value* Value::FindPath(const std::string& path) const {
    return const_cast<Value*>(this)->FindPath(path);
}

bool Value::SetPath(const std::string& path, Value v) {
    std::vector<Seg> segs;
    if (!ParsePath(path, segs)) return false;
    Value* cur = this;
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        const Seg& s = segs[i];
        const Seg& next = segs[i + 1];
        Value* child = nullptr;
        if (s.isIndex) {
            if (cur->GetType() != Type::Array) {
                cur->type_ = Type::Array;
                cur->arr_.clear();
            }
            child = &cur->At(s.index);
        } else {
            if (cur->GetType() != Type::Object) {
                cur->type_ = Type::Object;
                cur->obj_.clear();
            }
            child = &(*cur)[s.key];
        }
        // 下一段需要什么容器就先建出来
        if (next.isIndex && child->GetType() != Type::Array) {
            child->type_ = Type::Array;
            child->arr_.clear();
            child->obj_.clear();
            child->s_.clear();
        } else if (!next.isIndex && child->GetType() != Type::Object) {
            child->type_ = Type::Object;
            child->obj_.clear();
            child->arr_.clear();
            child->s_.clear();
        }
        cur = child;
    }
    const Seg& last = segs.back();
    if (last.isIndex) {
        if (cur->GetType() != Type::Array) {
            cur->type_ = Type::Array;
            cur->arr_.clear();
        }
        cur->At(last.index) = std::move(v);
    } else {
        if (cur->GetType() != Type::Object) {
            cur->type_ = Type::Object;
            cur->obj_.clear();
        }
        (*cur)[last.key] = std::move(v);
    }
    return true;
}

bool Value::RemovePath(const std::string& path) {
    std::vector<Seg> segs;
    if (!ParsePath(path, segs)) return false;
    if (segs.size() == 1) return segs[0].isIndex ? RemoveAt(segs[0].index) : Remove(segs[0].key);

    std::string parent;
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        if (i) parent += '.';
        parent += segs[i].isIndex ? ("[" + std::to_string(segs[i].index) + "]") : segs[i].key;
    }
    // 直接走一遍拿到父容器
    Value* cur = this;
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        const Seg& s = segs[i];
        if (s.isIndex) {
            if (cur->GetType() != Type::Array || s.index >= cur->arr_.size()) return false;
            cur = &cur->arr_[s.index];
        } else {
            Value* next = cur->Find(s.key);
            if (!next) return false;
            cur = next;
        }
    }
    const Seg& last = segs.back();
    if (last.isIndex) {
        if (cur->GetType() != Type::Array || last.index >= cur->arr_.size()) return false;
        return cur->RemoveAt(last.index);
    }
    return cur->Remove(last.key);
}

// ---------- 顶层 ----------

bool Parse(const std::string& text, Value& out, std::string* err, bool allowLoose) {
    Parser p(text, allowLoose);
    return p.Run(out, err);
}

std::string Dump(const Value& v, bool pretty, int indent) {
    std::string out;
    out.reserve(1024);
    DumpTo(v, out, pretty, indent, 0);
    if (pretty) out += '\n';
    return out;
}

bool Config::Load(const std::wstring& path, std::string* err) {
    std::string text;
    if (!ReadAll(path, text, err)) return false;
    Value v;
    if (!Parse(text, v, err, true)) return false;
    if (!v.IsObject()) {
        if (err) *err = "root is not an object";
        return false;
    }
    root_ = std::move(v);
    path_ = path;
    Stat(path, mtimeMs_, size_);
    return true;
}

bool Config::LoadText(const std::string& text, std::string* err) {
    Value v;
    if (!Parse(text, v, err, true)) return false;
    root_ = std::move(v);
    return true;
}

bool Config::Save(std::string* err, bool pretty) {
    if (path_.empty()) {
        if (err) *err = "no path set";
        return false;
    }
    return SaveAs(path_, pretty, err);
}

bool Config::SaveAs(const std::wstring& path, bool pretty, std::string* err) {
    std::string text = Dump(root_, pretty);
    if (!WriteAtomic(path, text, err)) return false;
    path_ = path;
    Stat(path, mtimeMs_, size_);
    return true;
}

bool Config::Changed() const {
    if (path_.empty()) return false;
    int64_t ms = 0;
    uint64_t sz = 0;
    if (!Stat(path_, ms, sz)) return false;
    return ms != mtimeMs_ || sz != size_;
}

bool Config::Reload(std::string* err) {
    if (path_.empty()) {
        if (err) *err = "no path set";
        return false;
    }
    return Load(path_, err);
}

bool Config::Has(const std::string& path) const { return root_.FindPath(path) != nullptr; }

bool Config::Remove(const std::string& path) { return root_.RemovePath(path); }

bool Config::GetBool(const std::string& path, bool def) const {
    const Value* v = root_.FindPath(path);
    return v ? v->AsBool(def) : def;
}

int64_t Config::GetInt(const std::string& path, int64_t def) const {
    const Value* v = root_.FindPath(path);
    return v ? v->AsInt(def) : def;
}

double Config::GetDouble(const std::string& path, double def) const {
    const Value* v = root_.FindPath(path);
    return v ? v->AsDouble(def) : def;
}

std::string Config::GetString(const std::string& path, const std::string& def) const {
    const Value* v = root_.FindPath(path);
    return v ? v->AsString(def) : def;
}

bool Config::Set(const std::string& path, const Value& v) { return root_.SetPath(path, v); }

} // namespace xgcfg
