/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "rapidyaml-0.12.1.hpp"
#include "xemu-sim-profile-reader.h"

namespace {

struct Val {
    enum T { NUL, BOOL, INT, FLT, STR, SEQ, MAP, OTHER } t = NUL;
    bool b = false;
    long long i = 0;
    bool big = false;
    double d = 0;
    std::string s;
    std::vector<Val> seq;
    std::vector<std::pair<Val, Val>> map;

    bool is_str() const { return t == STR; }
    bool is_int() const { return t == INT; }
    bool is_num() const { return t == INT || t == FLT; }
    double num() const { return t == INT && !big ? (double)i : d; }
    bool nonempty_str() const { return t == STR && !s.empty(); }

    const Val *get(const char *k) const
    {
        if (t != MAP) {
            return nullptr;
        }
        const Val *hit = nullptr;
        for (const auto &kv : map) {
            if (kv.first.t == STR && kv.first.s == k) {
                hit = &kv.second;
            }
        }
        return hit;
    }
    bool has(const char *k) const { return get(k) != nullptr; }
};

const std::regex &re(int k)
{
    static const std::regex R[] = {
        std::regex("yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF"),
        std::regex("[-+]?0b[0-1_]+|[-+]?0[0-7_]+|[-+]?(?:0|[1-9][0-9_]*)|[-+]?0x[0-9a-fA-F_]+"
                   "|[-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+"),
        std::regex("[-+]?(?:[0-9][0-9_]*)\\.[0-9_]*(?:[eE][-+][0-9]+)?|\\.[0-9_]+(?:[eE][-+][0-9]+)?"
                   "|[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+\\.[0-9_]*|[-+]?\\.(?:inf|Inf|INF)|\\.(?:nan|NaN|NAN)"),
        std::regex("[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]"
                   "|[0-9][0-9][0-9][0-9]-[0-9][0-9]?-[0-9][0-9]?(?:[Tt]|[ \\t]+)[0-9][0-9]?:[0-9][0-9]:[0-9][0-9]"
                   "(?:\\.[0-9]*)?(?:[ \\t]*(?:Z|[-+][0-9][0-9]?(?::[0-9][0-9])?))?"),
    };
    return R[k];
}

double sexagesimal(const std::string &digits)
{
    double v = 0;
    size_t at = 0;
    while (at <= digits.size()) {
        size_t c = digits.find(':', at);
        std::string part = digits.substr(at, c == std::string::npos ? std::string::npos : c - at);
        v = v * 60 + std::strtod(part.c_str(), nullptr);
        if (c == std::string::npos) {
            break;
        }
        at = c + 1;
    }
    return v;
}

Val plain_scalar(const std::string &text)
{
    Val v;
    v.s = text;
    if (text.empty() || text == "~" || text == "null" || text == "Null" || text == "NULL") {
        v.t = Val::NUL;
        return v;
    }
    if (std::regex_match(text, re(0))) {
        v.t = Val::BOOL;
        v.b = text[0] == 'y' || text[0] == 'Y' || text[0] == 't' || text[0] == 'T' ||
              text == "on" || text == "On" || text == "ON";
        return v;
    }
    std::string d = text;
    d.erase(std::remove(d.begin(), d.end(), '_'), d.end());
    bool neg = !d.empty() && d[0] == '-';
    if (!d.empty() && (d[0] == '-' || d[0] == '+')) {
        d = d.substr(1);
    }
    if (std::regex_match(text, re(1))) {
        long long x = 0;
        bool ok = true;
        double whole = 0;
        auto digits = [](const char *p, int base) {
            double w = 0;
            for (; *p; p++) {
                int c = std::tolower((unsigned char)*p);
                w = w * base + (c >= 'a' ? c - 'a' + 10 : c - '0');
            }
            return w;
        };
        errno = 0;
        if (d == "0") {
            x = 0;
        } else if (d.compare(0, 2, "0b") == 0) {
            x = std::strtoll(d.c_str() + 2, nullptr, 2);
            whole = digits(d.c_str() + 2, 2);
        } else if (d.compare(0, 2, "0x") == 0) {
            x = std::strtoll(d.c_str() + 2, nullptr, 16);
            whole = digits(d.c_str() + 2, 16);
        } else if (d[0] == '0') {
            x = std::strtoll(d.c_str(), nullptr, 8);
            whole = digits(d.c_str(), 8);
        } else if (d.find(':') != std::string::npos) {
            whole = sexagesimal(d);
            ok = whole < 9.2e18;
            x = ok ? (long long)whole : 0;
        } else {
            x = std::strtoll(d.c_str(), nullptr, 10);
            whole = std::strtod(d.c_str(), nullptr);
        }
        v.t = Val::INT;
        if (ok && errno == 0) {
            v.i = neg ? -x : x;
            return v;
        }
        v.big = true;
        v.i = neg ? LLONG_MIN : LLONG_MAX;
        v.d = neg ? -whole : whole;
        return v;
    }
    if (std::regex_match(text, re(2))) {
        std::string low = d;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return std::tolower(c); });
        v.t = Val::FLT;
        if (low == ".inf") {
            v.d = neg ? -INFINITY : INFINITY;
        } else if (low == ".nan") {
            v.d = NAN;
        } else if (d.find(':') != std::string::npos) {
            v.d = (neg ? -1 : 1) * sexagesimal(d);
        } else {
            v.d = (neg ? -1 : 1) * std::strtod(d.c_str(), nullptr);
        }
        return v;
    }
    v.t = std::regex_match(text, re(3)) ? Val::OTHER : Val::STR;
    return v;
}

struct Refused : std::runtime_error {
    using std::runtime_error::runtime_error;
};

ryml::Callbacks throwing_callbacks()
{
    ryml::Callbacks cb;
    cb.set_error_basic([](ryml::csubstr msg, const ryml::ErrorDataBasic &, void *) {
        throw Refused(std::string(msg.str, msg.len));
    });
    cb.set_error_parse([](ryml::csubstr msg, const ryml::ErrorDataParse &, void *) {
        throw Refused(std::string(msg.str, msg.len));
    });
    cb.set_error_visit([](ryml::csubstr msg, const ryml::ErrorDataVisit &, void *) {
        throw Refused(std::string(msg.str, msg.len));
    });
    return cb;
}

Val scalar_of(ryml::csubstr text, bool quoted)
{
    std::string s(text.str ? text.str : "", text.str ? text.len : 0);
    if (quoted) {
        Val v;
        v.t = Val::STR;
        v.s = s;
        return v;
    }
    return plain_scalar(s);
}

bool py_equal(const Val &a, const Val &b)
{
    auto numeric = [](const Val &x) { return x.t == Val::INT || x.t == Val::FLT || x.t == Val::BOOL; };
    if (a.t == Val::STR || b.t == Val::STR) {
        return a.t == b.t && a.s == b.s;
    }
    if (numeric(a) && numeric(b)) {
        if (a.t == Val::INT && b.t == Val::INT && !a.big && !b.big) {
            return a.i == b.i;
        }
        double x = a.t == Val::BOOL ? a.b : a.num(), y = b.t == Val::BOOL ? b.b : b.num();
        return x == y;
    }
    if (a.t == Val::NUL || b.t == Val::NUL) {
        return a.t == b.t;
    }
    return a.t == Val::OTHER && b.t == Val::OTHER && a.s == b.s;
}

Val tagged(Val v, ryml::csubstr tag, bool scalar, bool quoted)
{
    std::string name(tag.str, tag.len);
    if (name == "!") {
        return v;
    }
    ryml::YamlTag_e t = ryml::to_tag(tag);
    auto no = [&](const char *why) { throw Refused("the tag '" + name + "': " + why); };
    if (t == ryml::TAG_MAP || t == ryml::TAG_SEQ) {
        if (v.t != (t == ryml::TAG_MAP ? Val::MAP : Val::SEQ)) {
            no("on a node of another kind");
        }
        return v;
    }
    if (!scalar) {
        no("on a mapping or a sequence");
    }
    std::string text = v.s;
    Val p = plain_scalar(text);
    switch (t) {
    case ryml::TAG_STR:
        v.t = Val::STR;
        return v;
    case ryml::TAG_NULL:
        v.t = Val::NUL;
        return v;
    case ryml::TAG_INT:
        if (p.t != Val::INT) {
            no("the text is not an integer");
        }
        return p;
    case ryml::TAG_FLOAT:
        if (p.t == Val::INT) {
            p.d = p.num();
            p.t = Val::FLT;
            p.big = false;
        } else if (p.t != Val::FLT) {
            no("the text is not a float");
        }
        return p;
    case ryml::TAG_BOOL:
        if (p.t != Val::BOOL) {
            no("the text is not a bool");
        }
        return p;
    default:
        (void)quoted;
        no("not a type this reader reads (the app's loader refuses a local tag)");
    }
    return v;
}

Val convert(ryml::ConstNodeRef n)
{
    Val v;
    if (n.is_map()) {
        v.t = Val::MAP;
        std::map<std::string, size_t> str_keys;
        for (ryml::ConstNodeRef ch : n.children()) {
            Val k = ch.has_key() ? scalar_of(ch.key(), ch.is_key_quoted()) : Val();
            if (ch.has_key() && ch.has_key_tag()) {
                k = tagged(std::move(k), ch.key_tag(), true, ch.is_key_quoted());
            }
            Val x = convert(ch);
            size_t at = v.map.size();
            if (k.t == Val::STR) {
                auto it = str_keys.find(k.s);
                if (it != str_keys.end()) {
                    at = it->second;
                } else {
                    str_keys[k.s] = at;
                }
            } else {
                for (size_t j = 0; j < v.map.size(); j++) {
                    if (py_equal(v.map[j].first, k)) {
                        at = j;
                        break;
                    }
                }
            }
            if (at < v.map.size()) {
                v.map[at].second = std::move(x);
            } else {
                v.map.emplace_back(std::move(k), std::move(x));
            }
        }
    } else if (n.is_seq()) {
        v.t = Val::SEQ;
        for (ryml::ConstNodeRef ch : n.children()) {
            v.seq.push_back(convert(ch));
        }
    } else if (n.has_val()) {
        v = scalar_of(n.val(), n.is_val_quoted());
    }
    if (n.has_val_tag()) {
        v = tagged(std::move(v), n.val_tag(), !n.is_map() && !n.is_seq(), n.has_val() && n.is_val_quoted());
    }
    return v;
}

std::string unreadable_bytes(const std::string &text)
{
    const unsigned char *p = (const unsigned char *)text.data(), *e = p + text.size();
    while (p < e) {
        uint32_t c = *p;
        int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0 || e - p < n + 1) {
            return "not UTF-8 (a byte that starts no character, or a character cut short)";
        }
        c = n == 0 ? c : c & (0x3F >> n);
        for (int k = 1; k <= n; k++) {
            if ((p[k] & 0xC0) != 0x80) {
                return "not UTF-8 (a broken multi-byte character)";
            }
            c = (c << 6) | (p[k] & 0x3F);
        }
        static const uint32_t least[] = { 0, 0x80, 0x800, 0x10000 };
        if (c < least[n] || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
            return "not UTF-8 (an overlong form, a surrogate or a value past U+10FFFF)";
        }
        bool printable = c == 0x9 || c == 0xA || c == 0xD || (c >= 0x20 && c <= 0x7E) || c == 0x85 ||
                         (c >= 0xA0 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFD) || c >= 0x10000;
        if (!printable) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "a character the app's loader does not accept (U+%04X)", (unsigned)c);
            return buf;
        }
        p += n + 1;
    }
    return "";
}

void check_tree_bounds(const ryml::Tree &t)
{
    if (t.size() == 0) {
        return;
    }
    const size_t cap = t.capacity();
    std::vector<uint64_t> nodes(cap, 0);
    std::vector<uint32_t> height(cap, 0);
    std::vector<char> on_path(cap, 0);
    std::vector<ryml::id_type> key_target(cap, ryml::NONE), val_target(cap, ryml::NONE);
    std::map<std::string, std::pair<ryml::id_type, bool>> anchors;
    const uint64_t cut = (uint64_t)SIM_PROFILE_MAX_NODES + 1;
    struct Frame {
        ryml::id_type id;
        bool entered;
        uint32_t depth;
    };
    std::vector<Frame> stack{ { t.root_id(), false, 1 } };
    while (!stack.empty()) {
        ryml::id_type id = stack.back().id;
        uint32_t depth = stack.back().depth;
        if (!stack.back().entered) {
            stack.back().entered = true;
            if (depth > SIM_PROFILE_MAX_DEPTH) {
                throw Refused("nested deeper than " + std::to_string(SIM_PROFILE_MAX_DEPTH) + " levels");
            }
            on_path[id] = 1;
            nodes[id] = 1;
            height[id] = 1;
            for (int side = 0; side < 2; side++) {
                bool ref = side ? t.is_val_ref(id) : t.is_key_ref(id);
                if (ref) {
                    ryml::csubstr name = side ? t.val_ref(id) : t.key_ref(id);
                    auto a = anchors.find(std::string(name.str, name.len));
                    if (a != anchors.end()) {
                        if (!a->second.second && on_path[a->second.first]) {
                            throw Refused("an alias inside its own anchor (a recursive structure)");
                        }
                        (side ? val_target : key_target)[id] = a->second.first;
                    }
                }
            }
            if (t.has_key_anchor(id)) {
                ryml::csubstr a = t.key_anchor(id);
                anchors[std::string(a.str, a.len)] = { id, true };
            }
            if (t.has_val_anchor(id)) {
                ryml::csubstr a = t.val_anchor(id);
                anchors[std::string(a.str, a.len)] = { id, false };
            }
            std::vector<ryml::id_type> kids;
            for (ryml::id_type c = t.first_child(id); c != ryml::NONE; c = t.next_sibling(c)) {
                kids.push_back(c);
            }
            for (auto c = kids.rbegin(); c != kids.rend(); ++c) {
                stack.push_back({ *c, false, depth + 1 });
            }
            continue;
        }
        stack.pop_back();
        on_path[id] = 0;
        if (key_target[id] != ryml::NONE) {
            nodes[id] = std::min(cut, nodes[id] + 1);
        }
        if (val_target[id] != ryml::NONE) {
            ryml::id_type a = val_target[id];
            nodes[id] = std::min(cut, nodes[id] + nodes[a]);
            height[id] = std::max(height[id], height[a]);
        }
        if (nodes[id] >= cut) {
            throw Refused("more than " + std::to_string(SIM_PROFILE_MAX_NODES) + " nodes with its aliases expanded");
        }
        if (height[id] > SIM_PROFILE_MAX_DEPTH) {
            throw Refused("nested deeper than " + std::to_string(SIM_PROFILE_MAX_DEPTH) +
                          " levels with its aliases expanded");
        }
        if (!stack.empty()) {
            ryml::id_type p = t.parent(id);
            if (p != ryml::NONE) {
                nodes[p] = std::min(cut, nodes[p] + nodes[id]);
                height[p] = std::max(height[p], height[id] + 1);
            }
        }
    }
}

void check_flow_depth(const std::string &text)
{
    enum { PLAIN, SQ, DQ, COMMENT } st = PLAIN;
    int depth = 0;
    char last = '\n';
    char prev = '\n';
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        switch (st) {
        case COMMENT:
            if (c == '\n') {
                st = PLAIN;
                last = '\n';
            }
            break;
        case DQ:
            if (c == '\\') {
                i++;
            } else if (c == '"') {
                st = PLAIN;
                last = '"';
            }
            break;
        case SQ:
            if (c == '\'') {
                if (i + 1 < text.size() && text[i + 1] == '\'') {
                    i++;
                } else {
                    st = PLAIN;
                    last = '\'';
                }
            }
            break;
        case PLAIN:
            if (c == '\n') {
                last = '\n';
            } else if (c == '#' && (prev == ' ' || prev == '\t' || prev == '\n')) {
                st = COMMENT;
            } else if ((c == '\'' || c == '"') &&
                       (prev == ' ' || prev == '\t' || prev == '\n' || prev == '[' || prev == '{' || prev == ',') &&
                       std::strchr("\n:-,[{?", last)) {
                st = c == '"' ? DQ : SQ;
            } else if (c == '[' || c == '{') {
                if (++depth > SIM_PROFILE_MAX_DEPTH) {
                    throw Refused("nested deeper than " + std::to_string(SIM_PROFILE_MAX_DEPTH) + " levels");
                }
            } else if ((c == ']' || c == '}') && depth > 0) {
                depth--;
            }
            if (c != ' ' && c != '\t' && c != '\n' && st == PLAIN) {
                last = c;
            }
            break;
        }
        prev = c;
    }
}

struct MergeKeyRefused : Refused {
    using Refused::Refused;
};

void check_merge_key(const ryml::Tree &t)
{
    std::vector<ryml::id_type> stack = { t.root_id() };
    while (!stack.empty()) {
        ryml::id_type n = stack.back();
        stack.pop_back();
        if (t.has_key(n) && !t.is_key_quoted(n) && t.key(n) == "<<") {
            throw MergeKeyRefused("the YAML merge key << is not read: write the keys out");
        }
        for (ryml::id_type c = t.first_child(n); c != ryml::NONE; c = t.next_sibling(c)) {
            stack.push_back(c);
        }
    }
}

Val parse_file(const std::string &path, const std::string &text)
{
    check_flow_depth(text);
#ifdef SIM_PROFILE_READER_DRILL_DEFAULT_CALLBACKS
    ryml::Callbacks cb;
    (void)throwing_callbacks;
#else
    ryml::Callbacks cb = throwing_callbacks();
#endif
    ryml::EventHandlerTree handler(cb);
    ryml::Parser parser(&handler);
    ryml::Tree tree(cb);
    ryml::parse_in_arena(&parser, ryml::to_csubstr(path), ryml::csubstr(text.data(), text.size()), &tree);
    check_tree_bounds(tree);
    check_merge_key(tree);
    tree.resolve();
    ryml::ConstNodeRef root = tree.crootref();
    if (root.is_stream()) {
        if (root.num_children() == 0) {
            return Val();
        }
        if (root.num_children() > 1) {
            throw Refused("more than one YAML document in the file");
        }
        root = root.first_child();
    }
    return convert(root);
}

struct Doc {
    std::string kind, id, origin;
    Val v;
};

struct Fail {
    std::string code, where, why;
};

struct Fails : std::vector<Fail> {
    std::string at;
    void err(const char *code, const std::string &path, const std::string &why)
    {
        push_back({ code, at + (path.empty() ? "" : "." + path), why });
    }
};

const char *const KINDS[] = { "axis", "button", "hat", "detent", "relative" };
const char *const DOC_KINDS[] = { "template", "device", "calibration", "bindings", "profile",
                                  "rig", "ports", "target", "vr_catalogue" };
const char *const SOURCES[] = { "sdl_joystick", "sdl_gamepad", "keyboard", "mouse", "hid_usb", "vr_virtual" };
const char *const POLICIES[] = { "half_positive", "half_negative", "absolute", "full_range", "split" };
const char *const HAT_DIRECTIONS[] = { "up", "down", "left", "right" };
const char *const INTEGRATE_REFUSED[] = { "window", "deadzone", "curve", "saturation" };

template <size_t N> bool one_of(const std::string &s, const char *const (&set)[N])
{
    for (const char *w : set) {
        if (s == w) {
            return true;
        }
    }
    return false;
}

bool truthy(const Val *v)
{
    if (!v) {
        return false;
    }
    switch (v->t) {
    case Val::NUL: return false;
    case Val::BOOL: return v->b;
    case Val::INT: return v->i != 0;
    case Val::FLT: return v->d != 0;
    case Val::STR: return !v->s.empty();
    case Val::SEQ: return !v->seq.empty();
    case Val::MAP: return !v->map.empty();
    case Val::OTHER: return true;
    }
    return false;
}

bool is_code(const Val *v)
{
    return v && v->is_int() && v->i >= 0;
}

bool range_of(const Val &m, SimRange *r)
{
    const Val *a = m.get("min"), *b = m.get("rest"), *c = m.get("max");
    if (!a || !b || !c || !a->is_num() || !b->is_num() || !c->is_num()) {
        return false;
    }
    *r = { a->num(), b->num(), c->num() };
    return true;
}

void check_range(Fails &f, const std::string &path, const SimRange &r)
{
    if (!std::isfinite(r.min) || !std::isfinite(r.rest) || !std::isfinite(r.max)) {
        f.err("DESCRIPTOR_INVALID", path, "min, rest and max are finite numbers");
        return;
    }
    if (r.min == r.max) {
        f.err("DESCRIPTOR_INVALID", path, "min equals max: no travel");
    }
    double lo = std::min(r.min, r.max), hi = std::max(r.min, r.max);
    if (!(lo <= r.rest && r.rest <= hi)) {
        f.err("DESCRIPTOR_INVALID", path, "rest lies outside [min, max]");
    }
}

bool range_numbers(Fails &f, const std::string &path, const Val &m)
{
    bool ok = true;
    for (const char *k : { "min", "rest", "max" }) {
        const Val *x = m.get(k);
        if (!x) {
            continue;
        }
        if (!x->is_num()) {
            f.err("RANGE_NOT_NUMBER", path + "." + k, std::string(k) + " is a number, got '" + x->s + "'");
            ok = false;
        } else if (!std::isfinite(x->num())) {
            f.err("RANGE_NOT_FINITE", path + "." + k, std::string(k) + " is a finite number, got '" + x->s + "'");
            ok = false;
        }
    }
    return ok;
}

bool bipolar(const SimRange &r)
{
    double lo = std::min(r.min, r.max), hi = std::max(r.min, r.max);
    return lo < r.rest && r.rest < hi;
}

std::string idx(const char *key, size_t i)
{
    return std::string(key) + "[" + std::to_string(i) + "]";
}

void check_code_range(Fails &f, const std::string &path, const Val &sig, const std::string &kind, bool evdev)
{
    long long table = kind == "axis" ? 0x3f : kind == "hat" ? 0x3f - 1 : kind == "button" ? 0x2ff :
                      kind == "relative" ? 0x0f : -1;
    if (const Val *code = sig.get("code")) {
        if (code->is_int() && evdev && table >= 0 && (code->big || code->i < 0 || code->i > table)) {
            f.err("CODE_RANGE", path + ".code", "code " + code->s + " is outside the Linux evdev table for a " + kind +
                  " input: 0.." + std::to_string(table));
        } else if (code->is_int() && (code->big || code->i > INT_MAX)) {
            f.err("CODE_RANGE", path + ".code", "code " + code->s + " does not fit this core's code (0.." +
                  std::to_string(INT_MAX) + "): it is refused, never narrowed");
        }
    }
    const Val *sc = sig.get("scancode");
    if (evdev && sc && sc->is_int() && (sc->big || sc->i < 0 || sc->i > 0x2ff)) {
        f.err("CODE_RANGE", path + ".scancode", "scancode " + sc->s + " is outside the Linux evdev table for a key: "
                                                "0.." + std::to_string(0x2ff));
    }
}

void check_signal(Fails &f, const std::string &path, const Val &sig, bool keyboard, bool mouse, bool evdev)
{
    const Val *k = sig.get("kind");
    if (!k || !k->is_str() || !one_of(k->s, KINDS)) {
        f.err("KIND_UNKNOWN", path, "kind must be one of axis, button, hat, detent, relative");
        return;
    }
    const std::string &kind = k->s;
    check_code_range(f, path, sig, kind, evdev);
    if (mouse) {
        if (kind != "button" && kind != "relative") {
            f.err("MOUSE_INPUT_KIND", path, "a mouse input is a button or a relative axis");
            return;
        }
        if (!is_code(sig.get("code"))) {
            f.err("CODE_REQUIRED", path, "a mouse input carries its evdev code (an integer)");
        }
    }
    bool has_range = sig.has("min") || sig.has("rest") || sig.has("max");
    if (keyboard) {
        if (kind != "button") {
            f.err("KEYBOARD_NOT_BUTTON", path, "a keyboard-class input is a button");
        }
        if (!sig.has("scancode")) {
            f.err("SCANCODE_REQUIRED", path, "a keyboard-class input's only identity is its scancode");
        }
        if (has_range || sig.has("analog")) {
            f.err("DESCRIPTOR_ON_KEY", path, "a key has no travel");
        }
        return;
    }
    if (kind == "axis") {
        SimRange r;
        if (!range_numbers(f, path, sig)) {
        } else if (!range_of(sig, &r)) {
            f.err("DESCRIPTOR_INCOMPLETE", path, "an axis needs min, rest and max");
        } else {
            check_range(f, path, r);
        }
        const Val *dz = sig.get("deadzone");
        if (dz && dz->t != Val::NUL && !(dz->is_num() && dz->num() >= 0 && dz->num() < 1)) {
            f.err("DEADZONE_RANGE", path, "deadzone is a fraction of travel in [0, 1)");
        }
        const Val *inv = sig.get("invert");
        if (inv && inv->t != Val::BOOL) {
            f.err("INVERT_TYPE", path, "invert is true or false");
        }
        if (sig.has("code") && !is_code(sig.get("code"))) {
            f.err("CODE_TYPE", path, "an axis's code is its evdev ABS_* code, an integer");
        }
    } else if ((kind == "button" || kind == "hat") && sig.has("code") && !mouse && !is_code(sig.get("code"))) {
        f.err("CODE_TYPE", path, "a button's or hat's code is its evdev code, an integer");
    }
    if (kind == "button") {
        if (has_range) {
            f.err("DESCRIPTOR_ON_BUTTON", path, "a button has no min/rest/max; declare `analog:`");
        }
        if (const Val *an = sig.get("analog")) {
            SimRange r;
            if (!range_numbers(f, path + ".analog", *an)) {
            } else if (!range_of(*an, &r)) {
                f.err("DESCRIPTOR_INCOMPLETE", path + ".analog", "analog needs min, rest and max");
            } else {
                check_range(f, path + ".analog", r);
            }
        }
    } else if (kind == "relative") {
        if (has_range || sig.has("analog")) {
            f.err("DESCRIPTOR_ON_RELATIVE", path, "a relative input reports motion, not a position");
        }
    } else if (kind == "detent") {
        const Val *pos = sig.get("positions");
        if (!pos || pos->t == Val::NUL) {
            f.err("DETENT_POSITIONS_REQUIRED", path, "a detent (lever) declares its positions: [...], the values its "
                                                     "reading takes");
        } else if (pos->t != Val::SEQ || pos->seq.empty()) {
            f.err("DETENT_POSITIONS", path, "positions is a non-empty list");
        } else {
            for (const Val &q : pos->seq) {
                if (!(q.is_int() || q.is_str())) {
                    f.err("DETENT_POSITIONS", path, "each position is an integer or text, never a bool, a float or a "
                                                    "list (positions compare by type and value)");
                    break;
                }
            }
        }
    }
}

void unique_ids(Fails &f, const Val *items, const char *key)
{
    std::set<std::string> seen;
    if (!items || items->t != Val::SEQ) {
        return;
    }
    for (size_t i = 0; i < items->seq.size(); i++) {
        const Val *id = items->seq[i].get("id");
        if (!id || id->t == Val::NUL || (id->is_str() && id->s.empty())) {
            f.err("MISSING_ID", idx(key, i), "every entry needs an id");
            continue;
        }
        if (!id->is_str()) {
            f.err("ID_TYPE", idx(key, i) + ".id", "an id is text; '" + id->s + "' is not (quote it)");
        }
        if (!seen.insert(id->s).second) {
            f.err("DUPLICATE_ID", idx(key, i), "id '" + id->s + "' appears twice");
        }
    }
}

bool usb_id(const Val &m)
{
    if (m.t != Val::MAP) {
        return false;
    }
    for (const auto &kv : m.map) {
        if (!(kv.first.is_str() && (kv.first.s == "vid" || kv.first.s == "pid" || kv.first.s == "mode"))) {
            return false;
        }
    }
    for (const char *k : { "vid", "pid" }) {
        const Val *x = m.get(k);
        if (!x || !x->is_int() || x->i < 0 || x->i > 0xFFFF) {
            return false;
        }
    }
    const Val *mode = m.get("mode");
    return !mode || mode->is_str();
}

bool bind_entries(const Val *bind, std::vector<Val> *out)
{
    if (!bind) {
        return false;
    }
    if (bind->t == Val::MAP) {
        for (const auto &kv : bind->map) {
            Val e;
            e.t = Val::MAP;
            e.map.emplace_back(plain_scalar("source"), kv.first);
            e.map.emplace_back(plain_scalar("target"), kv.second);
            out->push_back(std::move(e));
        }
        return true;
    }
    if (bind->t == Val::SEQ) {
        *out = bind->seq;
        return true;
    }
    return false;
}

void check_document(Fails &f, const Doc &d)
{
    const Val &v = d.v;
    const Val *schema = v.get("schema");
    if (!schema || !schema->is_str() || schema->s != "sim-profile/1") {
        f.err("SCHEMA_UNKNOWN", "schema", "expected 'sim-profile/1'");
    }
    if (!one_of(d.kind, DOC_KINDS)) {
        f.err("KIND_UNKNOWN", "kind", "not a sim-profile/1 document kind");
        return;
    }
    if (d.kind != "vr_catalogue" && d.id.empty()) {
        f.err("MISSING_ID", "id", "every document has a string id");
    }
    if (d.kind == "template") {
        const Val *m = v.get("mirror");
        if (m && m->t != Val::BOOL) {
            f.err("MIRROR_TYPE", "mirror", "mirror is true or false");
        }
    } else if (d.kind == "device" || d.kind == "target") {
        const Val *src = v.get("source");
        std::string source = src && src->is_str() ? src->s : "";
        if (d.kind == "device" && !one_of(source, SOURCES)) {
            f.err("SOURCE_UNKNOWN", "source", "source must be one of sdl_joystick, sdl_gamepad, keyboard, "
                                              "mouse, hid_usb, vr_virtual");
        }
        if (d.kind == "device") {
            if (v.has("match") && !usb_id(*v.get("match"))) {
                f.err("MATCH_SHAPE", "match", "match is {vid, pid, mode?}");
            }
            if (const Val *also = v.get("match_also")) {
                bool ok = v.has("match") && also->t == Val::SEQ;
                for (size_t i = 0; ok && i < also->seq.size(); i++) {
                    ok = usb_id(also->seq[i]);
                }
                if (!ok) {
                    f.err("MATCH_SHAPE", "match_also", "match_also is a list of {vid, pid, mode?}, beside a match");
                }
            }
        }
        const char *key = d.kind == "target" ? "controls" : "inputs";
        if (d.kind == "device" && source == "vr_virtual") {
            const Val *cat = v.get("catalogue");
            if (!cat || !cat->is_str()) {
                f.err("CATALOGUE_REQUIRED", "catalogue", "a vr_virtual device names its catalogue device");
            }
            if (v.has("inputs")) {
                f.err("INPUTS_ON_VIRTUAL", "inputs", "a virtual device's controls come from the catalogue");
            }
        } else {
            const Val *items = v.get(key);
            if (!items || items->t != Val::SEQ || items->seq.empty()) {
                f.err("INPUTS_REQUIRED", key, std::string("a ") + d.kind + " lists its " + key);
            } else {
                unique_ids(f, items, key);
                for (size_t i = 0; i < items->seq.size(); i++) {
                    if (items->seq[i].t == Val::MAP) {
                        check_signal(f, idx(key, i), items->seq[i], source == "keyboard",
                                     d.kind == "device" && source == "mouse",
                                     d.kind == "device" && (source == "sdl_joystick" || source == "keyboard" ||
                                                            source == "mouse"));
                        const Val *rtr = items->seq[i].get("returns_to_rest");
                        const Val *knd = items->seq[i].get("kind");
                        if (d.kind == "target" && rtr &&
                            !(rtr->t == Val::BOOL && knd && knd->is_str() && knd->s == "axis")) {
                            f.err("RETURNS_TO_REST", idx(key, i) + ".returns_to_rest",
                                  "returns_to_rest is true or false, on an axis control only");
                        }
                    }
                }
            }
        }
    } else if (d.kind == "calibration") {
        const Val *pp = v.get("port_path");
        if (!pp || !pp->nonempty_str()) {
            f.err("PORT_PATH_REQUIRED", "port_path", "calibration is per physical unit, keyed by port path");
        }
        const Val *dev = v.get("device");
        if (!dev || !dev->is_str()) {
            f.err("MISSING_REF", "device", "calibration names the device profile it measures");
        }
        const Val *in = v.get("inputs");
        if (in && in->t != Val::NUL && in->t != Val::MAP) {
            f.err("CALIBRATION_SHAPE", "inputs", "a calibration's inputs is a mapping {<input id>: {min, rest, max}}");
        } else if (in) {
            for (const auto &kv : in->map) {
                SimRange r;
                std::string p = "inputs." + kv.first.s;
                if (!range_numbers(f, p, kv.second)) {
                } else if (!range_of(kv.second, &r)) {
                    f.err("DESCRIPTOR_INCOMPLETE", p, "measured min, rest and max");
                } else {
                    check_range(f, p, r);
                }
            }
        }
    } else if (d.kind == "bindings") {
        for (const char *ref : { "device", "target" }) {
            const Val *r = v.get(ref);
            if (!r || !r->is_str()) {
                f.err("MISSING_REF", ref, std::string("bindings names its ") + ref);
            }
        }
        std::vector<Val> entries;
        if (!bind_entries(v.get("bind"), &entries) || entries.empty()) {
            f.err("BIND_REQUIRED", "bind", "bindings has a non-empty bind list (or doc 31 mapping)");
        }
        for (size_t i = 0; i < entries.size(); i++) {
            const Val &e = entries[i];
            std::string p = idx("bind", i);
            const Val *src = e.get("source");
            if (e.t != Val::MAP || !truthy(src)) {
                f.err("BIND_SOURCE", p, "each binding names its source input");
                continue;
            }
            bool has_t = e.has("target"), has_ts = e.has("targets");
            const Val *pol = e.get("policy");
            bool split = pol && pol->is_str() && pol->s == "split";
            if (has_t == has_ts) {
                f.err("BIND_TARGET", p, "each binding has exactly one of target / targets");
            }
            if (has_ts && (!split || e.get("targets")->t != Val::SEQ || e.get("targets")->seq.size() != 2)) {
                f.err("SPLIT_SHAPE", p, "`targets` is only for policy: split, with exactly two targets");
            }
            if (split && !has_ts) {
                f.err("SPLIT_SHAPE", p, "policy split feeds two targets: use `targets: [a, b]`");
            }
            if (pol && !(pol->is_str() && one_of(pol->s, POLICIES))) {
                f.err("POLICY_UNKNOWN", p, "policy must be one of half_positive, half_negative, absolute, "
                                           "full_range, split");
            }
            if (e.has("response") && e.get("response")->t != Val::MAP) {
                f.err("RESPONSE_SHAPE", p + ".response", "response, when given, is a mapping (window, deadzone, curve, "
                                                         "saturation, invert, mode)");
            }
            if (const Val *resp = e.get("response")) {
                const Val *w = resp->get("window");
                if (w && !(w->t == Val::SEQ && w->seq.size() == 2 && w->seq[0].is_num() && w->seq[1].is_num() &&
                           0 <= w->seq[0].num() && w->seq[0].num() < w->seq[1].num() && w->seq[1].num() <= 1)) {
                    f.err("RESPONSE_WINDOW", p + ".response.window", "window is [lo, hi] with 0 <= lo < hi <= 1");
                }
                const Val *inv = resp->get("invert");
                if (inv && inv->t != Val::BOOL) {
                    f.err("INVERT_TYPE", p + ".response.invert", "invert is true or false");
                }
                if (!resp->has("mode")) {
                    const Val *rdz = resp->get("deadzone"), *sat = resp->get("saturation"), *cur = resp->get("curve");
                    double hi = (w && w->t == Val::SEQ && w->seq.size() == 2 && w->seq[1].is_num()) ? w->seq[1].num()
                                                                                                    : 1.0;
                    if (rdz && !(rdz->is_num() && rdz->num() >= 0 && rdz->num() < hi)) {
                        f.err("RESPONSE_DEADZONE", p + ".response.deadzone",
                              "deadzone is a fraction of travel in [0, window hi)");
                    }
                    if (sat && !(sat->is_num() && sat->num() > 0 && sat->num() <= 1)) {
                        f.err("RESPONSE_SATURATION", p + ".response.saturation", "saturation is in (0, 1]");
                    }
                    if (cur && !(cur->is_num() && cur->num() > 0)) {
                        f.err("RESPONSE_CURVE", p + ".response.curve", "curve is a number > 0");
                    }
                }
                if (const Val *mode = resp->get("mode")) {
                    if (!(mode->is_str() && mode->s == "integrate")) {
                        f.err("RESPONSE_MODE_UNKNOWN", p + ".response.mode", "mode must be integrate");
                    } else {
                        const Val *n = resp->get("counts_per_travel");
                        if (!(n && n->is_num() && n->num() > 0)) {
                            f.err("INTEGRATE_COUNTS", p + ".response.counts_per_travel",
                                  "integrate needs counts_per_travel > 0");
                        }
                        for (const char *k : INTEGRATE_REFUSED) {
                            if (resp->has(k)) {
                                f.err("INTEGRATE_FIELD", p + ".response." + k,
                                      std::string(k) + " shapes a position; not defined for mode integrate");
                            }
                        }
                        const Val *rc = resp->get("recentre");
                        if (rc && !rc->nonempty_str()) {
                            f.err("RECENTRE_UNKNOWN", p + ".response.recentre",
                                  "recentre names a button input of the same device");
                        }
                    }
                }
            }
        }
    } else if (d.kind == "rig") {
        const Val *game = v.get("game");
        if (!(game && game->get("title_id") && game->get("title_id")->is_int())) {
            f.err("GAME_TITLE_ID", "game.title_id", "a rig is keyed to a game by its XBE title ID (an integer)");
        }
        const Val *t = v.get("target");
        if (!t || !t->is_str()) {
            f.err("MISSING_REF", "target", "a rig names the emulated device it feeds");
        }
        const Val *devs = v.get("devices");
        if (!devs || devs->t != Val::SEQ || devs->seq.empty()) {
            f.err("DEVICES_REQUIRED", "devices", "a rig lists at least one device");
        } else {
            for (size_t i = 0; i < devs->seq.size(); i++) {
                const Val &x = devs->seq[i];
                const Val *pr = x.get("profile"), *bd = x.get("bindings");
                if (x.t != Val::MAP || !pr || !pr->is_str() || !bd || !bd->is_str()) {
                    f.err("RIG_DEVICE", idx("devices", i), "each rig device names a profile and a bindings file");
                }
            }
        }
    } else if (d.kind == "ports" && v.has("ports")) {
        if (v.has("assign")) {
            f.err("PORTS_FORM_MIXED", "assign", "a ports document holds `ports:` or the old `assign:`, not both");
        }
        const Val *entries = v.get("ports");
        if (entries->t != Val::MAP) {
            f.err("PORTS_SHAPE", "ports", "ports is a mapping {1: {target, profile, devices}, ...}");
            return;
        }
        for (const auto &kv : entries->map) {
            std::string p = "ports." + kv.first.s;
            if (!(kv.first.is_int() && kv.first.i >= 1 && kv.first.i <= SIM_PROFILE_MAX_PORTS)) {
                f.err("PORT_RANGE", p, "ports are 1..4");
                continue;
            }
            const Val &e = kv.second;
            if (e.t == Val::NUL) {
                continue;
            }
            if (e.t != Val::MAP) {
                f.err("PORT_SHAPE", p, "a port is a mapping: target, profile, devices, outputs");
                continue;
            }
            for (const char *key : { "target", "profile" }) {
                const Val *x = e.get(key);
                if (x && x->t != Val::NUL && !x->nonempty_str()) {
                    f.err("PORT_SHAPE", p + "." + key, std::string(key) + " is an id string");
                }
            }
            const Val *tg = e.get("target"), *pf = e.get("profile"), *devs = e.get("devices");
            bool off = !tg || tg->t == Val::NUL;
            bool holds = truthy(devs) || (pf && pf->t != Val::NUL);
            if (off && holds) {
                f.err("PORT_OFF", p, "the port has no emulated controller (it is off) but holds devices or a profile");
            }
            if (devs && devs->t != Val::SEQ) {
                f.err("PORT_SHAPE", p + ".devices", "devices is a list");
                continue;
            }
            for (size_t i = 0; devs && i < devs->seq.size(); i++) {
                const Val &x = devs->seq[i];
                std::string dp = p + "." + idx("devices", i);
                const Val *dv = x.get("device");
                if (x.t != Val::MAP || !dv || !dv->nonempty_str()) {
                    f.err("PORT_DEVICE", dp, "each port device names its device profile: {device: <id>}");
                    continue;
                }
                if (x.has("bindings")) {
                    f.err("PORT_DEVICE_BINDINGS", dp + ".bindings", "a port names its devices; the bindings are "
                                                                    "its PROFILE's");
                }
                const Val *pp = x.get("port_path");
                if (pp && !pp->nonempty_str()) {
                    f.err("PORT_PATH_SHAPE", dp + ".port_path", "port_path is the unit's OS port path");
                }
            }
            if (const Val *lu = e.get("last_used")) {
                bool ok = lu->t == Val::MAP || lu->t == Val::NUL;
                for (size_t i = 0; ok && lu->t == Val::MAP && i < lu->map.size(); i++) {
                    const Val &k = lu->map[i].first;
                    ok = k.is_int() && k.i >= 0 && k.i <= 0xFFFFFFFFLL && lu->map[i].second.nonempty_str();
                }
                if (!ok) {
                    f.err("LAST_USED_SHAPE", p + ".last_used", "last_used is a mapping {<title id>: <profile id>}");
                }
            }
        }
    } else if (d.kind == "ports") {
        const Val *assign = v.get("assign");
        if (!assign || assign->t != Val::MAP || assign->map.empty()) {
            f.err("ASSIGN_REQUIRED", "assign", "ports assigns rigs to ports: {1: <rig id>, ...}");
        } else {
            for (const auto &kv : assign->map) {
                if (!(kv.first.is_int() && kv.first.i >= 1 && kv.first.i <= SIM_PROFILE_MAX_PORTS)) {
                    f.err("PORT_RANGE", "assign." + kv.first.s, "ports are 1..4");
                }
            }
        }
    } else if (d.kind == "profile") {
        const Val *t = v.get("target");
        if (!t || !t->is_str()) {
            f.err("MISSING_REF", "target", "a profile names the emulated controller it maps to");
        }
        if (const Val *name = v.get("name")) {
            bool blank = name->is_str() && name->s.find_first_not_of(" \t\r\n") == std::string::npos;
            if (!name->is_str() || blank) {
                f.err("PROFILE_NAME", "name", "a profile's name is a non-empty string");
            }
        }
        if (const Val *game = v.get("game")) {
            if (!(game->get("title_id") && game->get("title_id")->is_int())) {
                f.err("GAME_TITLE_ID", "game.title_id", "a profile's game is keyed by its XBE title ID (an integer)");
            }
        }
        const Val *b = v.get("bindings");
        bool ok = !b || b->t == Val::MAP;
        for (size_t i = 0; ok && b && i < b->map.size(); i++) {
            ok = b->map[i].first.nonempty_str() && b->map[i].second.nonempty_str();
        }
        if (!ok) {
            f.err("BINDINGS_MAP_SHAPE", "bindings", "bindings is a mapping {<device>: <bindings id>} (may be empty)");
        }
    }
}

struct DevC {
    const Doc *doc;
    std::vector<SimInput> in;
    std::deque<std::string> strs;
    std::deque<std::vector<const char *>> pos;
    std::deque<std::string> pos_kinds;
    std::deque<std::vector<long long>> pos_ints;
    SimSourceKind source = SIM_SRC_HID_USB;
    bool has_match = false;
    uint16_t vid = 0, pid = 0;
    std::vector<uint16_t> also;
    std::vector<const Val *> ids;
    std::vector<const Val *> positions;

    const SimInput *find(const std::string &id) const
    {
        for (const auto &x : in) {
            if (id == x.id) {
                return &x;
            }
        }
        return nullptr;
    }
    const SimInput *find(const Val &name) const
    {
        for (size_t k = 0; k < in.size(); k++) {
            if (py_equal(*ids[k], name)) {
                return &in[k];
            }
        }
        return nullptr;
    }
    const Val *positions_of(const SimInput *x) const { return positions[(size_t)(x - in.data())]; }
};

struct BindC {
    const Doc *doc;
    std::string device, target;
    std::vector<SimBinding> b;
    std::deque<std::string> strs;
};

struct PortsC {
    SimPort port[SIM_PROFILE_MAX_PORTS];
    std::deque<std::vector<SimPortDevice>> devs;
    std::deque<std::vector<SimCalibrated>> cal;
    std::deque<std::string> strs;
};

}

struct SimProfileSet {
    std::deque<Doc> docs;
    std::map<std::string, std::map<std::string, const Doc *>> filed;
    std::deque<Fail> refused;
    std::vector<SimRefusal> refusal_view;
    std::map<std::string, DevC> devices, targets;
    std::map<std::string, BindC> bindings;
    std::map<std::string, const Doc *> profiles, rigs;
    std::map<std::string, PortsC> ports;
    std::vector<const Doc *> shape_refused;
    std::map<std::string, std::set<std::string>> claimed;

    void refuse(const Fail &f) { refused.push_back(f); }
    const Doc *get(const std::string &kind, const std::string &id) const
    {
        auto k = filed.find(kind);
        if (k == filed.end()) {
            return nullptr;
        }
        auto d = k->second.find(id);
        return d == k->second.end() ? nullptr : d->second;
    }
    void unfile(const Doc *d) { filed[d->kind].erase(d->id); }
};

namespace {

const char *keep(std::deque<std::string> &pool, const std::string &s)
{
    pool.push_back(s);
    return pool.back().c_str();
}

SimSourceKind source_kind(const std::string &s)
{
    if (s == "sdl_joystick") return SIM_SRC_SDL_JOYSTICK;
    if (s == "sdl_gamepad") return SIM_SRC_SDL_GAMEPAD;
    if (s == "keyboard") return SIM_SRC_KEYBOARD;
    if (s == "mouse") return SIM_SRC_MOUSE;
    if (s == "vr_virtual") return SIM_SRC_VR_VIRTUAL;
    return SIM_SRC_HID_USB;
}

SimInputKind input_kind(const std::string &s)
{
    if (s == "button") return SIM_IN_BUTTON;
    if (s == "hat") return SIM_IN_HAT;
    if (s == "detent") return SIM_IN_DETENT;
    if (s == "relative") return SIM_IN_RELATIVE;
    return SIM_IN_AXIS;
}

void compile_inputs(DevC &c, const char *key)
{
    const Val *items = c.doc->v.get(key);
    if (!items || items->t != Val::SEQ) {
        return;
    }
    c.in.reserve(items->seq.size());
    for (const Val &sig : items->seq) {
        SimInput x{};
        x.id = keep(c.strs, sig.get("id")->s);
        x.kind = input_kind(sig.get("kind")->s);
        const Val *code = sig.get("code");
        x.code = is_code(code) && !code->big && code->i <= INT_MAX ? (int)code->i : -1;
        x.has_range = range_of(sig, &x.range);
        x.bipolar = x.has_range && bipolar(x.range);
        const Val *inv = sig.get("invert");
        x.invert = inv && inv->t == Val::BOOL && inv->b;
        const Val *dz = sig.get("deadzone");
        x.deadzone = dz && dz->is_num() ? dz->num() : 0;
        const Val *rtr = sig.get("returns_to_rest");
        x.stays_put = x.kind == SIM_IN_AXIS && rtr && rtr->t == Val::BOOL && !rtr->b;
        if (const Val *an = sig.get("analog")) {
            x.analog = range_of(*an, &x.analog_range);
        }
        if (const Val *sc = sig.get("scancode")) {
            x.scancode = keep(c.strs, sc->s);
        }
        const Val *pos = sig.get("positions");
        if (pos && pos->t == Val::SEQ) {
            c.pos.emplace_back();
            c.pos_kinds.emplace_back();
            c.pos_ints.emplace_back();
            for (const Val &p : pos->seq) {
                c.pos.back().push_back(keep(c.strs, p.s));
                c.pos_kinds.back().push_back(p.is_int() ? 'I' : 'S');
                c.pos_ints.back().push_back(p.is_int() ? p.i : 0);
            }
            x.npositions = (int)c.pos.back().size();
            x.positions = c.pos.back().data();
            x.position_kinds = c.pos_kinds.back().c_str();
            x.position_ints = c.pos_ints.back().data();
        }
        c.in.push_back(x);
        c.ids.push_back(sig.get("id"));
        c.positions.push_back(pos && pos->t == Val::SEQ ? pos : nullptr);
    }
}

struct Sig {
    SimInputKind kind;
    int polarity;
    bool analog;
};

Sig sig_of(const SimInput &x)
{
    if (x.kind == SIM_IN_AXIS) {
        return { x.kind, x.has_range ? (x.bipolar ? 1 : 0) : -1, false };
    }
    if (x.kind == SIM_IN_BUTTON && x.analog) {
        return { x.kind, bipolar(x.analog_range) ? 1 : 0, true };
    }
    return { x.kind, -1, false };
}

bool same_position(const Val &a, const Val &b)
{
    if (a.t != b.t) {
        return false;
    }
    switch (a.t) {
    case Val::INT: return a.i == b.i && a.big == b.big && (!a.big || a.d == b.d);
    case Val::STR: return a.s == b.s;
    default: return py_equal(a, b);
    }
}

bool same_positions(const Val *a, const Val *b)
{
    if (!a || !b || a->t != Val::SEQ || b->t != Val::SEQ || a->seq.size() != b->seq.size()) {
        return false;
    }
    for (size_t i = 0; i < a->seq.size(); i++) {
        if (!same_position(a->seq[i], b->seq[i])) {
            return false;
        }
    }
    return true;
}

int position_index(const Val &want, const Val *positions)
{
    for (size_t i = 0; positions && i < positions->seq.size(); i++) {
        if (same_position(want, positions->seq[i])) {
            return (int)i;
        }
    }
    return -1;
}

void check_pair(Fails &f, const std::string &p, const Val &e, const SimInput &s, const SimInput &t,
                const Val *src_positions, const Val *tgt_positions)
{
#ifdef SIM_PROFILE_READER_DRILL_ACCEPT_PAIRS
    return;
#endif
    Sig src = sig_of(s), tgt = sig_of(t);
    const Val *pol = e.get("policy");
    bool has_policy = pol && pol->t != Val::NUL;
    if (src.kind == SIM_IN_RELATIVE && tgt.kind == SIM_IN_AXIS) {
        if (has_policy) {
            f.err("POLICY_ON_RELATIVE", p, "a relative source has no polarity");
        }
        const Val *resp = e.get("response");
        const Val *mode = resp ? resp->get("mode") : nullptr;
        if (!(mode && mode->is_str() && mode->s == "integrate")) {
            f.err("RELATIVE_NEEDS_INTEGRATE", p, "relative -> axis needs response: {mode: integrate, "
                                                 "counts_per_travel: N}");
        }
        return;
    }
    if (src.kind == SIM_IN_AXIS &&
        (tgt.kind == SIM_IN_AXIS || (tgt.kind == SIM_IN_BUTTON && tgt.analog && !e.has("threshold")))) {
        if (src.polarity < 0 || tgt.polarity < 0) {
            return;
        }
        bool known = pol && pol->is_str() && one_of(pol->s, POLICIES);
        if (src.polarity != tgt.polarity && !known) {
            f.err("POLARITY_POLICY_REQUIRED", p, std::string(src.polarity ? "bipolar" : "unipolar") +
                  " source -> " + (tgt.polarity ? "bipolar" : "unipolar") +
                  " target needs a declared policy; a polarity mismatch is never converted silently");
        }
        return;
    }
    if (src.kind == SIM_IN_AXIS && tgt.kind == SIM_IN_BUTTON) {
        const Val *th = e.get("threshold"), *hy = e.get("hysteresis");
        if (!(th && th->is_num() && hy && hy->is_num())) {
            f.err("AXIS_TO_BUTTON_THRESHOLD", p, "axis -> button declares threshold and hysteresis");
            return;
        }
        double t0 = th->num(), h = hy->num();
        if (src.polarity < 0) {
            if (h < 0) {
                f.err("AXIS_TO_BUTTON_HYSTERESIS", p, "hysteresis is >= 0");
            }
            return;
        }
        double lo = src.polarity ? -1.0 : 0.0, hi = 1.0;
        if (t0 == 0.0 || !(lo <= t0 && t0 <= hi)) {
            f.err("AXIS_TO_BUTTON_THRESHOLD_RANGE", p, "threshold is outside the source's travel, or 0 (its sign picks "
                                                       "the direction)");
            return;
        }
        double release = t0 > 0 ? t0 - h : t0 + h;
        if (h < 0 || !(lo < release && release < hi)) {
            f.err("AXIS_TO_BUTTON_HYSTERESIS", p, "hysteresis puts the release point where the source never passes; "
                                                  "hysteresis is >= 0");
        }
        return;
    }
    if (src.kind == SIM_IN_BUTTON && tgt.kind == SIM_IN_AXIS) {
        const Val *val = e.get("value"), *rate = e.get("rate"), *ramp = e.get("ramp");
        if (!t.stays_put) {
            if (rate) {
                f.err("RATE_ON_SPRING_BACK", p, "this target springs back to rest: a key sets it to a value while held");
            }
            if (!(val && val->is_num())) {
                f.err("BUTTON_TO_AXIS_VALUE", p, "button -> axis declares the value it asserts");
            } else if (tgt.polarity >= 0) {
                double lo = tgt.polarity ? -1.0 : 0.0;
                if (!(lo <= val->num() && val->num() <= 1.0) || val->num() == 0.0) {
                    f.err("BUTTON_TO_AXIS_VALUE_RANGE", p, "value is outside the target's canonical travel, or at rest (0)");
                }
            }
            if (ramp) {
                bool ok = ramp->t == Val::MAP && !ramp->map.empty();
                for (size_t i = 0; ok && i < ramp->map.size(); i++) {
                    const Val &k = ramp->map[i].first, &v = ramp->map[i].second;
                    ok = k.t == Val::STR && (k.s == "press_ms" || k.s == "release_ms") && v.is_num() && v.num() >= 0;
                }
                if (!ok) {
                    f.err("BUTTON_TO_AXIS_RAMP", p, "ramp is {press_ms, release_ms}, each a number of ms >= 0");
                }
            }
            return;
        }
        if (val) {
            f.err("VALUE_ON_STAY_PUT", p, "this target stays where it is left: a key moves it at a rate while held");
        }
        if (ramp) {
            f.err("RAMP_ON_STAY_PUT", p, "a ramp shapes a spring-back position; a stay-put target moves at its rate");
        }
        if (!(rate && rate->is_num())) {
            if (!val) {
                f.err("BUTTON_TO_AXIS_RATE", p, "button -> a stay-put axis declares its rate (full travels per second)");
            }
        } else if (!(0.01 <= std::fabs(rate->num()) && std::fabs(rate->num()) <= 100.0)) {
            f.err("BUTTON_TO_AXIS_RATE_RANGE", p, "|rate| must be 0.01..100 full travels per second");
        }
        return;
    }
    if (src.kind == SIM_IN_BUTTON && tgt.kind == SIM_IN_BUTTON) {
        return;
    }
    if (src.kind == SIM_IN_DETENT && tgt.kind == SIM_IN_BUTTON) {
        const Val *pos = e.get("position");
        if (!pos) {
            f.err("DETENT_POSITION", p, "detent -> button names the position that becomes the press");
        } else if (position_index(*pos, src_positions) < 0) {
            f.err("DETENT_POSITION", p, "position '" + pos->s + "' is not one of the source's positions (by type and "
                                        "value: 1 is not \"1\")");
        }
        return;
    }
    if (src.kind == SIM_IN_DETENT && tgt.kind == SIM_IN_DETENT) {
        if (!same_positions(src_positions, tgt_positions)) {
            f.err("DETENT_POSITIONS_DIFFER", p, "detent -> detent passes the reading straight through, so both levers "
                                                "have the same positions in the same order");
        }
        return;
    }
    if (src.kind == SIM_IN_HAT && tgt.kind == SIM_IN_BUTTON) {
        const Val *dir = e.get("direction");
        if (!(dir && dir->is_str() && one_of(dir->s, HAT_DIRECTIONS))) {
            f.err("HAT_DIRECTION", p, "hat -> button names a direction: up, down, left, right");
        }
        return;
    }
    f.err("UNSUPPORTED_KIND_PAIR", p, std::string(KINDS[src.kind]) + " -> " + KINDS[tgt.kind] +
          " is not defined by the design");
}

SimPolicy policy_of(const Val *pol)
{
    if (!pol || !pol->is_str()) return SIM_POLICY_NONE;
    if (pol->s == "half_positive") return SIM_POLICY_HALF_POSITIVE;
    if (pol->s == "half_negative") return SIM_POLICY_HALF_NEGATIVE;
    if (pol->s == "absolute") return SIM_POLICY_ABSOLUTE;
    if (pol->s == "full_range") return SIM_POLICY_FULL_RANGE;
    if (pol->s == "split") return SIM_POLICY_SPLIT;
    return SIM_POLICY_NONE;
}

bool compile_bindings(SimProfileSet &set, const Doc &d, Fails &f, bool report_only = false)
{
    const Val *dv = d.v.get("device"), *tv = d.v.get("target");
    if (!dv || !dv->is_str() || !tv || !tv->is_str()) {
        return false;
    }
    std::string dev = dv->s, tgt = tv->s;
    auto di = set.devices.find(dev);
    auto ti = set.targets.find(tgt);
    if (di == set.devices.end()) {
        f.err("UNRESOLVED_REF", "device", "no device '" + dev + "' (absent, or refused)");
        return false;
    }
    if (ti == set.targets.end()) {
        f.err("UNRESOLVED_REF", "target", "no target '" + tgt + "' (absent, or refused)");
        return false;
    }
    BindC tmp;
    BindC &c = report_only ? tmp : set.bindings[d.id];
    c.doc = &d;
    c.device = dev;
    c.target = tgt;
    if (di->second.source == SIM_SRC_VR_VIRTUAL) {
        return true;
    }
    std::vector<Val> entries;
    bind_entries(d.v.get("bind"), &entries);
    c.b.reserve(entries.size());
    if (report_only) {
        std::vector<Val> keep;
        for (const Val &e : entries) {
            const Val *sv = e.get("source");
            if (e.t == Val::MAP && sv && sv->is_str() && (e.has("target") != e.has("targets"))) {
                keep.push_back(e);
            }
        }
        entries.swap(keep);
    }
    for (size_t i = 0; i < entries.size(); i++) {
        const Val &e = entries[i];
        std::string p = idx("bind", i);
        const Val *sv = e.get("source");
        const SimInput *src = sv->is_str() ? di->second.find(*sv) : nullptr;
        if (!src) {
            f.err("UNKNOWN_SOURCE", p, "'" + e.get("source")->s + "' is not an input of device '" + dev + "'");
            continue;
        }
        const Val *resp = e.get("response");
        if (resp && src->kind != SIM_IN_AXIS && src->kind != SIM_IN_RELATIVE) {
            f.err("RESPONSE_ON_NON_AXIS", p, "axis response applies to an axis (or relative) source only");
        }
        const Val *mode = resp ? resp->get("mode") : nullptr;
        bool integrate = mode && mode->is_str() && mode->s == "integrate";
        const SimInput *recentre = nullptr;
        if (integrate) {
            if (src->kind == SIM_IN_AXIS) {
                f.err("INTEGRATE_ON_ABSOLUTE", p + ".response.mode", "integrate is for a relative source");
            }
            const Val *rc = resp->get("recentre");
            if (rc && rc->nonempty_str()) {
                recentre = di->second.find(*rc);
                if (!recentre || recentre->kind != SIM_IN_BUTTON) {
                    f.err("RECENTRE_UNKNOWN", p + ".response.recentre", "'" + rc->s +
                          "' is not a button of device '" + dev + "'");
                }
            }
        }
        if (policy_of(e.get("policy")) == SIM_POLICY_SPLIT && src->kind == SIM_IN_RELATIVE && !integrate) {
            f.err("RELATIVE_NEEDS_INTEGRATE", p, "a relative source reports motion: split it only through response: "
                                                 "{mode: integrate, ...}");
        }
        std::vector<const Val *> names;
        if (const Val *ts = e.get("targets")) {
            for (const Val &n : ts->seq) {
                names.push_back(&n);
            }
        } else {
            names.push_back(e.get("target"));
        }
        const SimInput *t1 = nullptr, *t2 = nullptr;
        for (const Val *n : names) {
            const SimInput *t = n ? ti->second.find(*n) : nullptr;
            if (!t) {
                f.err("UNKNOWN_TARGET_CONTROL", p, "'" + (n ? n->s : std::string()) +
                      "' is not a control of target '" + tgt + "'");
                continue;
            }
            (t1 ? t2 : t1) = t;
            if (policy_of(e.get("policy")) != SIM_POLICY_SPLIT) {
                check_pair(f, p, e, *src, *t, di->second.positions_of(src), ti->second.positions_of(t));
            }
        }
        if (!t1) {
            continue;
        }
        SimBinding b{};
        b.source = src;
        b.target = t1;
        b.target2 = t2;
        b.policy = policy_of(e.get("policy"));
        const Val *th = e.get("threshold"), *hy = e.get("hysteresis"), *val = e.get("value");
        b.has_threshold = th && th->is_num();
        b.threshold = b.has_threshold ? th->num() : 0;
        b.hysteresis = hy && hy->is_num() ? hy->num() : 0;
        b.has_value = val && val->is_num();
        b.value = b.has_value ? val->num() : 0;
        if (const Val *ramp = e.get("ramp")) {
            const Val *pm = ramp->get("press_ms"), *rm = ramp->get("release_ms");
            b.has_ramp = true;
            b.press_ms = pm && pm->is_num() ? pm->num() : 0;
            b.release_ms = rm && rm->is_num() ? rm->num() : 0;
        }
        const Val *rate = e.get("rate");
        b.has_rate = rate && rate->is_num();
        b.rate = b.has_rate ? rate->num() : 0;
        if (const Val *pos = e.get("position")) {
            int at = position_index(*pos, di->second.positions_of(src));
            b.position = at >= 0 ? src->positions[at] : keep(c.strs, pos->s);
        }
        if (const Val *dir = e.get("direction")) {
            b.direction = keep(c.strs, dir->s);
        }
        if (resp) {
            b.integrate = integrate;
            const Val *n = resp->get("counts_per_travel");
            b.counts_per_travel = n && n->is_num() ? n->num() : 0;
            b.recentre = recentre;
            const Val *inv = resp->get("invert");
            b.resp_invert = inv && inv->t == Val::BOOL && inv->b;
            const Val *w = resp->get("window");
            if (w && w->t == Val::SEQ && w->seq.size() == 2) {
                b.has_window = true;
                b.window_lo = w->seq[0].num();
                b.window_hi = w->seq[1].num();
            }
            const Val *cv = resp->get("curve"), *dz = resp->get("deadzone"), *sa = resp->get("saturation");
            b.has_curve = cv && cv->is_num();
            b.curve = b.has_curve ? cv->num() : 0;
            b.has_deadzone = dz && dz->is_num();
            b.resp_deadzone = b.has_deadzone ? dz->num() : 0;
            b.has_saturation = sa && sa->is_num();
            b.saturation = b.has_saturation ? sa->num() : 0;
        }
        c.b.push_back(b);
    }
    if (!f.empty() || report_only) {
        if (!report_only) {
            set.bindings.erase(d.id);
        }
        return false;
    }
    return true;
}

bool check_profile(SimProfileSet &set, const Doc &d, Fails &f)
{
    std::string target = d.v.get("target")->s;
    if (!set.targets.count(target)) {
        f.err("UNRESOLVED_REF", "target", "no target '" + target + "'");
    }
    if (const Val *b = d.v.get("bindings")) {
        for (const auto &kv : b->map) {
            auto bi = set.bindings.find(kv.second.s);
            if (bi == set.bindings.end()) {
                f.err("UNRESOLVED_REF", "bindings." + kv.first.s, "no bindings '" + kv.second.s +
                      "' (absent, or refused)");
            } else if (bi->second.target != target) {
                f.err("BINDINGS_TARGET_MISMATCH", "bindings." + kv.first.s, "bindings '" + kv.second.s +
                      "' targets '" + bi->second.target + "'; the profile maps to '" + target + "'");
            }
        }
    }
    return f.empty();
}

bool check_rig(SimProfileSet &set, const Doc &d, Fails &f)
{
    std::string target = d.v.get("target")->s;
    if (!set.targets.count(target)) {
        f.err("UNRESOLVED_REF", "target", "no target '" + target + "'");
    }
    const Val *devs = d.v.get("devices");
    for (size_t i = 0; i < devs->seq.size(); i++) {
        const Val &x = devs->seq[i];
        std::string p = idx("devices", i), dev = x.get("profile")->s, bid = x.get("bindings")->s;
        if (!set.devices.count(dev)) {
            f.err("UNRESOLVED_REF", p + ".profile", "no device '" + dev + "'");
            continue;
        }
        auto bi = set.bindings.find(bid);
        if (bi == set.bindings.end()) {
            f.err("UNRESOLVED_REF", p + ".bindings", "no bindings '" + bid + "' (absent, or refused)");
        } else if (bi->second.device != dev) {
            f.err("BINDINGS_DEVICE_MISMATCH", p, "bindings '" + bid + "' is written for device '" +
                  bi->second.device + "', not '" + dev + "'");
        } else if (bi->second.target != target) {
            f.err("BINDINGS_TARGET_MISMATCH", p, "bindings '" + bid + "' targets '" + bi->second.target +
                  "'; the rig feeds '" + target + "'");
        }
    }
    return f.empty();
}

SimPortDevice port_device(SimProfileSet &set, PortsC &pc, const std::string &dev, const std::string &key,
                          const Val *port_path, const BindC *b)
{
    const DevC &dc = set.devices.at(dev);
    SimPortDevice o{};
    o.device_id = keep(pc.strs, dev);
    o.key = keep(pc.strs, key);
    o.port_path = port_path && port_path->nonempty_str() ? keep(pc.strs, port_path->s) : nullptr;
    o.source = dc.source;
    o.has_match = dc.has_match;
    o.vid = dc.vid;
    o.pid = dc.pid;
    o.nmatch_also = (int)dc.also.size() / 2;
    o.match_also = dc.also.empty() ? nullptr : dc.also.data();
    o.ninputs = (int)dc.in.size();
    o.inputs = dc.in.data();
    o.usable = dc.source != SIM_SRC_VR_VIRTUAL;
    if (b && o.usable) {
        o.nbindings = (int)b->b.size();
        o.bindings = b->b.data();
    }
    auto k = set.filed.find("calibration");
    if (o.port_path && k != set.filed.end()) {
        for (const auto &it : k->second) {
            const Val &cv = it.second->v;
            if (cv.get("device")->s != dev || cv.get("port_path")->s != o.port_path) {
                continue;
            }
            pc.cal.emplace_back();
            if (const Val *in = cv.get("inputs")) {
                for (const auto &kv : in->map) {
                    const SimInput *x = dc.find(kv.first);
                    SimRange r;
                    if (x && range_of(kv.second, &r)) {
                        pc.cal.back().push_back({ x, r });
                    }
                }
            }
            o.ncalibrated = (int)pc.cal.back().size();
            o.calibrated = pc.cal.back().data();
            break;
        }
    }
    return o;
}

void compose_ports(SimProfileSet &set, const Doc &d)
{
    PortsC &pc = set.ports[d.id];
    for (int i = 0; i < SIM_PROFILE_MAX_PORTS; i++) {
        pc.port[i] = SimPort{};
        pc.port[i].port = i + 1;
    }
    const Val *entries = d.v.get("ports");
    const Val *assign = d.v.get("assign");
    const Val *list = entries ? entries : assign;
    for (const auto &kv : list->map) {
        int n = (int)kv.first.i;
        SimPort &sp = pc.port[n - 1];
        Fails f;
        f.at = "ports:" + d.id + "." + (entries ? "ports." : "assign.") + std::to_string(n);
        std::string target, prof;
        const Val *devs = nullptr;
        const Doc *pdoc = nullptr;
        if (entries) {
            const Val &e = kv.second;
            if (e.t != Val::MAP || !e.get("target") || !e.get("target")->is_str()) {
                continue;
            }
            target = e.get("target")->s;
            const Val *pf = e.get("profile");
            prof = pf && pf->is_str() ? pf->s : "";
            devs = e.get("devices");
        } else {
            prof = kv.second.s;
            auto r = kv.second.is_str() ? set.rigs.find(prof) : set.rigs.end();
            if (r == set.rigs.end()) {
                f.err("UNRESOLVED_REF", "", "no rig '" + prof + "' (absent, or refused)");
                for (auto &x : f) set.refuse(x);
                continue;
            }
            pdoc = r->second;
            target = pdoc->v.get("target")->s;
            devs = pdoc->v.get("devices");
        }
        if (!set.targets.count(target)) {
            f.err("UNRESOLVED_REF", "target", "no target '" + target + "': the port is off");
            for (auto &x : f) set.refuse(x);
            continue;
        }
        sp.target = keep(pc.strs, target);
        if (entries && !prof.empty()) {
            auto p = set.profiles.find(prof);
            if (p == set.profiles.end()) {
                f.err("UNRESOLVED_REF", "profile", "no profile '" + prof + "' (absent, or refused)");
            } else if (p->second->v.get("target")->s != target) {
                f.err("PORT_PROFILE_TARGET", "profile", "profile '" + prof + "' maps to '" +
                      p->second->v.get("target")->s + "'; the port is '" + target + "'");
            } else {
                pdoc = p->second;
            }
        }
        struct One { std::string dev, key; const Val *pp; const BindC *b; };
        std::vector<One> ones;
        auto key_of = [&](const Val &x) -> const Val * {
            const Val *inst = x.get("instance");
            return truthy(inst) ? inst : (entries ? x.get("device") : x.get("profile"));
        };
        for (size_t i = 0; devs && devs->t == Val::SEQ && i < devs->seq.size(); i++) {
            const Val &x = devs->seq[i];
            std::string dev = (entries ? x.get("device") : x.get("profile"))->s;
            const Val *kv = key_of(x);
            std::string key = kv->s;
            if (!set.devices.count(dev)) {
                f.err("UNRESOLVED_REF", idx("devices", i), "no device '" + dev + "' (absent, or refused)");
                continue;
            }
            const BindC *b = nullptr;
            std::string bid;
            if (pdoc && entries) {
                const Val *bm = pdoc->v.get("bindings");
                const Val *hit = bm && kv->is_str() ? bm->get(key.c_str()) : nullptr;
                bid = hit ? hit->s : "";
            } else if (pdoc) {
                for (size_t j = devs->seq.size(); j-- > 0;) {
                    if (py_equal(*key_of(devs->seq[j]), *kv)) {
                        bid = devs->seq[j].get("bindings")->s;
                        break;
                    }
                }
            }
            if (!bid.empty()) {
                auto bi = set.bindings.find(bid);
                if (bi == set.bindings.end()) {
                    f.err("UNRESOLVED_REF", idx("devices", i), "no bindings '" + bid + "'");
                } else if (bi->second.device != dev) {
                    f.err("BINDINGS_DEVICE_MISMATCH", idx("devices", i), "bindings '" + bid +
                          "' is written for device '" + bi->second.device + "', not '" + dev + "'");
                } else {
                    b = &bi->second;
                }
            }
            ones.push_back({ dev, key, x.get("port_path"), b });
        }
        if (!f.empty()) {
            sp.profile_lost = prof.empty() ? nullptr : keep(pc.strs, prof);
            pdoc = nullptr;
            for (auto &o : ones) {
                o.b = nullptr;
            }
            for (auto &x : f) set.refuse(x);
        }
        if (pdoc) {
            sp.profile = keep(pc.strs, prof);
            const Val *game = pdoc->v.get("game");
            const Val *tid = game ? game->get("title_id") : nullptr;
            if (tid && tid->is_int()) {
                sp.has_title_id = true;
                sp.title_id = (uint32_t)tid->i;
            }
        }
        pc.devs.emplace_back();
        for (const auto &o : ones) {
            pc.devs.back().push_back(port_device(set, pc, o.dev, o.key, o.pp, o.b));
        }
        sp.ndevices = (int)pc.devs.back().size();
        sp.devices = pc.devs.back().data();
    }
}

std::string read_file(const std::string &path, std::string *out)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return "cannot read the file";
    }
    if (!S_ISREG(st.st_mode)) {
        return "not a regular file";
    }
    std::string too_big = "larger than " + std::to_string(SIM_PROFILE_MAX_FILE_BYTES) + " bytes";
    if (st.st_size > SIM_PROFILE_MAX_FILE_BYTES) {
        return too_big;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return "cannot read the file";
    }
    std::vector<char> buf(SIM_PROFILE_MAX_FILE_BYTES + 1);
    in.read(buf.data(), (std::streamsize)buf.size());
    size_t n = (size_t)in.gcount();
    if (n > SIM_PROFILE_MAX_FILE_BYTES) {
        return too_big;
    }
    out->assign(buf.data(), n);
    return "";
}

bool yaml_name(const std::string &n)
{
    auto ends = [&](const char *s) {
        size_t l = std::strlen(s);
        return n.size() >= l && n.compare(n.size() - l, l, s) == 0;
    };
    return ends(".yaml") || ends(".yml");
}

void walk(SimProfileSet &set, const std::string &dir, std::vector<std::string> *files)
{
    DIR *d = opendir(dir.c_str());
    if (!d) {
        return;
    }
    std::vector<std::string> names, subs;
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") {
            continue;
        }
        std::string p = dir + "/" + n;
        struct stat st;
        if (lstat(p.c_str(), &st) != 0) {
            continue;
        }
        if (S_ISLNK(st.st_mode)) {
            struct stat to;
            if (stat(p.c_str(), &to) == 0 && S_ISDIR(to.st_mode)) {
                set.refuse({ "PARSE_ERROR", p, "a symbolic link to a folder: not followed (as the app's folder walk)" });
                continue;
            }
        }
        if (S_ISDIR(st.st_mode)) {
            subs.push_back(p);
        } else if (yaml_name(n)) {
            names.push_back(p);
        }
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    std::sort(subs.begin(), subs.end());
    files->insert(files->end(), names.begin(), names.end());
    for (const auto &s : subs) {
        walk(set, s, files);
    }
}

void add_file(SimProfileSet &set, const std::string &path)
{
    std::string text;
    std::string unread = read_file(path, &text);
    if (unread.empty()) {
        unread = unreadable_bytes(text);
    }
    if (!unread.empty()) {
        set.refuse({ "PARSE_ERROR", path, unread });
        return;
    }
    Val v;
    try {
        v = parse_file(path, text);
    } catch (const MergeKeyRefused &e) {
        set.refuse({ "MERGE_KEY", path, e.what() });
        return;
    } catch (const std::exception &e) {
        std::string why = e.what();
        set.refuse({ "PARSE_ERROR", path, why.substr(0, why.find('\n')) });
        return;
    }
    if (v.t != Val::MAP) {
        set.refuse({ "NOT_A_MAPPING", path, "a profile document is a YAML mapping" });
        return;
    }
    set.docs.push_back(Doc());
    Doc &d = set.docs.back();
    d.origin = path;
    d.v = std::move(v);
    const Val *k = d.v.get("kind"), *id = d.v.get("id");
    d.kind = k && k->is_str() ? k->s : "";
    d.id = id && id->is_str() ? id->s : "";
    if (d.kind == "vr_catalogue") {
        return;
    }
    bool claims = one_of(d.kind, DOC_KINDS) && !d.id.empty();
    if (claims && !set.claimed[d.kind].insert(d.id).second) {
        set.refuse({ "DUPLICATE_ID", d.kind + ":" + d.id, "a second " + d.kind + " with id '" + d.id + "' (" +
                     path + ")" });
        return;
    }
    Fails f;
    f.at = (d.kind.empty() ? "?" : d.kind) + ":" + (d.id.empty() ? path : d.id);
    check_document(f, d);
    if (!f.empty()) {
        for (auto &x : f) set.refuse(x);
        if (d.kind == "bindings") {
            set.shape_refused.push_back(&d);
        }
        return;
    }
    set.filed[d.kind][d.id] = &d;
}

void check_calibration(const SimProfileSet &set, Fails &f, const Doc &cal, const Val &inp, const Val &meas,
                       const SimInput &x)
{
    SimRange m, decl;
    if (!range_of(meas, &m)) {
        return;
    }
    bool has_decl = x.kind == SIM_IN_AXIS ? x.has_range : x.analog;
    decl = x.kind == SIM_IN_AXIS ? x.range : x.analog_range;
    if (has_decl) {
        double lo = std::min(decl.min, decl.max), hi = std::max(decl.min, decl.max);
        std::string outside;
        for (auto kv : { std::make_pair("min", m.min), std::make_pair("rest", m.rest), std::make_pair("max", m.max) }) {
            if (!(lo <= kv.second && kv.second <= hi)) {
                outside += std::string(outside.empty() ? "" : ", ") + kv.first;
            }
        }
        if (!outside.empty()) {
            f.err("CALIBRATION_OUT_OF_RANGE", "inputs." + inp.s, "its " + outside + " lie outside the device's declared "
                  "range: it was recorded against a different range for this device and must be recorded again");
        }
        if (bipolar(m) != bipolar(decl)) {
            std::set<std::string> seen;
            for (const Doc &b : set.docs) {
                if (b.kind != "bindings" || b.id.empty() || !seen.insert(b.id).second) {
                    continue;
                }
                const Val *bd = b.v.get("device"), *cd = cal.v.get("device");
                if (!bd || !cd || !py_equal(*bd, *cd)) {
                    continue;
                }
                std::vector<Val> entries;
                bind_entries(b.v.get("bind"), &entries);
                for (size_t i = 0; i < entries.size(); i++) {
                    const Val &e = entries[i];
                    const Val *src = e.get("source"), *pol = e.get("policy");
                    if (e.t != Val::MAP || !src || !py_equal(*src, inp) ||
                        (pol && pol->is_str() && one_of(pol->s, POLICIES))) {
                        continue;
                    }
                    f.err("CALIBRATION_POLARITY", "inputs." + inp.s, "this calibration makes '" + inp.s + "' " +
                          (bipolar(m) ? "bipolar" : "unipolar") + " where its device declares it " +
                          (bipolar(decl) ? "bipolar" : "unipolar") + "; bindings:" + b.id + " " + idx("bind", i) +
                          " reads it with no policy. Measure the rest at the stop, or declare a policy");
                }
            }
        }
    }
}

void compile(SimProfileSet &set)
{
    for (const char *kind : { "target", "device" }) {
        auto k = set.filed.find(kind);
        if (k == set.filed.end()) {
            continue;
        }
        for (auto &kv : k->second) {
            DevC &c = (std::strcmp(kind, "target") == 0 ? set.targets : set.devices)[kv.first];
            c.doc = kv.second;
            const Val &v = c.doc->v;
            const Val *src = v.get("source");
            c.source = source_kind(src && src->is_str() ? src->s : "");
            if (const Val *m = v.get("match")) {
                c.has_match = true;
                c.vid = (uint16_t)m->get("vid")->i;
                c.pid = (uint16_t)m->get("pid")->i;
            }
            if (const Val *also = v.get("match_also")) {
                for (const Val &m : also->seq) {
                    c.also.push_back((uint16_t)m.get("vid")->i);
                    c.also.push_back((uint16_t)m.get("pid")->i);
                }
            }
            compile_inputs(c, std::strcmp(kind, "target") == 0 ? "controls" : "inputs");
        }
    }
    auto pass = [&](const char *kind, auto fn) {
        auto k = set.filed.find(kind);
        if (k == set.filed.end()) {
            return;
        }
        std::vector<const Doc *> docs;
        for (auto &kv : k->second) {
            docs.push_back(kv.second);
        }
        for (const Doc *d : docs) {
            Fails f;
            f.at = std::string(kind) + ":" + d->id;
            if (!fn(*d, f)) {
                for (auto &x : f) set.refuse(x);
                set.unfile(d);
            }
        }
    };
    pass("calibration", [&](const Doc &d, Fails &f) {
        std::string dev = d.v.get("device")->s;
        auto dc = set.devices.find(dev);
        if (dc == set.devices.end()) {
            f.err("UNRESOLVED_REF", "device", "no device '" + dev + "' (absent, or refused)");
        } else if (const Val *in = d.v.get("inputs")) {
            for (const auto &kv : in->map) {
                const SimInput *x = dc->second.find(kv.first);
                if (!x) {
                    f.err("UNKNOWN_SOURCE", "inputs." + kv.first.s, "not an input of device '" + dev + "'");
                } else {
                    check_calibration(set, f, d, kv.first, kv.second, *x);
                }
            }
        }
        return f.empty();
    });
    pass("bindings", [&](const Doc &d, Fails &f) { return compile_bindings(set, d, f); });
    for (const Doc *d : set.shape_refused) {
        Fails f;
        f.at = "bindings:" + d->id;
        compile_bindings(set, *d, f, true);
        for (auto &x : f) set.refuse(x);
    }
    pass("profile", [&](const Doc &d, Fails &f) {
        if (!check_profile(set, d, f)) {
            return false;
        }
        set.profiles[d.id] = &d;
        return true;
    });
    pass("rig", [&](const Doc &d, Fails &f) {
        if (!check_rig(set, d, f)) {
            return false;
        }
        set.rigs[d.id] = &d;
        return true;
    });
    auto k = set.filed.find("ports");
    if (k != set.filed.end()) {
        for (auto &kv : k->second) {
            compose_ports(set, *kv.second);
        }
    }
}

}

extern "C" {

char sim_profile_scalar_kind(const char *text, bool quoted, long long *i, double *d)
{
    Val v = scalar_of(ryml::to_csubstr(text), quoted);
    if (i) {
        *i = v.t == Val::BOOL ? v.b : v.i;
    }
    if (d) {
        *d = v.d;
    }
    return "NBIFS??O"[v.t];
}

SimProfileSet *sim_profile_set_load(const char *builtin_dir, const char *profile_dir)
{
    SimProfileSet *set = new SimProfileSet();
    for (const char *dir : { builtin_dir, profile_dir }) {
        if (!dir || !*dir) {
            continue;
        }
        std::vector<std::string> files;
        walk(*set, dir, &files);
        for (const auto &p : files) {
            add_file(*set, p);
        }
    }
    try {
        compile(*set);
    } catch (const std::exception &e) {
        set->refuse({ "PARSE_ERROR", "the reader", e.what() });
        set->ports.clear();
    }
    for (const auto &f : set->refused) {
        set->refusal_view.push_back({ f.code.c_str(), f.where.c_str(), f.why.c_str() });
    }
    return set;
}

void sim_profile_set_free(SimProfileSet *set)
{
    delete set;
}

int sim_profile_set_nrefused(const SimProfileSet *set)
{
    return (int)set->refusal_view.size();
}

const SimRefusal *sim_profile_set_refusal(const SimProfileSet *set, int i)
{
    return i >= 0 && i < (int)set->refusal_view.size() ? &set->refusal_view[i] : nullptr;
}

int sim_profile_set_count(const SimProfileSet *set, const char *kind)
{
    auto k = set->filed.find(kind);
    return k == set->filed.end() ? 0 : (int)k->second.size();
}

bool sim_profile_set_has(const SimProfileSet *set, const char *kind, const char *id)
{
    return set->get(kind, id) != nullptr;
}

const SimInput *sim_profile_target_control(const SimProfileSet *set, const char *target, const char *control)
{
    auto t = set->targets.find(target);
    return t == set->targets.end() ? nullptr : t->second.find(control);
}

int sim_profile_set_ports(const SimProfileSet *set, const char *ports_id, SimPort out[SIM_PROFILE_MAX_PORTS])
{
    auto c = set->claimed.find("ports");
    if (!ports_id && (c == set->claimed.end() || c->second.empty())) {
        return -1;
    }
    auto p = set->ports.find(ports_id ? std::string(ports_id) : *c->second.begin());
    if (p == set->ports.end()) {
        return -1;
    }
    int on = 0;
    for (int i = 0; i < SIM_PROFILE_MAX_PORTS; i++) {
        out[i] = p->second.port[i];
        on += out[i].target != nullptr;
    }
    return on;
}

}
