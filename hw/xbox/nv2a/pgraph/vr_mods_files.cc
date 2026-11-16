/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "rapidyaml-0.12.1.hpp"
#include "vr_mods.h"

namespace {

struct Refused : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void refuse(const std::string &why)
{
    throw Refused(why);
}

std::string str(ryml::ConstNodeRef n)
{
    if (!n.has_val()) {
        return {};
    }
    ryml::csubstr v = n.val();
    return std::string(v.str, v.len);
}

uint32_t u32(ryml::ConstNodeRef n, const char *what)
{
    std::string s = str(n), d = s;
    if (d.size() > 2 && d[0] == '0' && (d[1] == 'x' || d[1] == 'X')) {
        d = d.substr(2);
    }
    bool ok = !d.empty() && d.size() <= 8;
    for (char c : d) {
        ok &= std::isxdigit((unsigned char)c) != 0;
    }
    if (!ok) {
        refuse(std::string(what) + " is not a hex number of up to 8 digits: '" +
               s + "'");
    }
    return (uint32_t)std::strtoul(d.c_str(), nullptr, 16);
}

uint32_t small(ryml::ConstNodeRef n, const char *what)
{
    std::string s = str(n);
    if (s.size() != 1 || !std::isdigit((unsigned char)s[0])) {
        refuse(std::string(what) + " is 1, 2 or 4");
    }
    return (uint32_t)(s[0] - '0');
}

float f32(ryml::ConstNodeRef n, const char *what)
{
    std::string s = str(n);
    char *end = nullptr;
    double v = s.empty() ? 0 : std::strtod(s.c_str(), &end);
    if (s.empty() || *end || !std::isfinite(v)) {
        refuse(std::string(what) + " is not a finite number: '" + s + "'");
    }
    return (float)v;
}

uint32_t bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

char *dup(const std::string &s)
{
    char *p = (char *)std::malloc(s.size() + 1);
    std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

void read_value(ryml::ConstNodeRef w, uint32_t *value, uint32_t *original,
                bool *has_original)
{
    bool v = w.has_child("value"), f = w.has_child("float");
    if (v == f) {
        refuse("a word gives exactly one of 'value' and 'float'");
    }
    *value = v ? u32(w["value"], "value") : bits(f32(w["float"], "float"));
    *has_original = w.has_child("original");
    if (*has_original) {
        *original = v ? u32(w["original"], "original")
                      : bits(f32(w["original"], "original"));
    }
}

void read_find(ryml::ConstNodeRef f, VrModFind *out)
{
    int forms = f.has_child("floats") + f.has_child("words") +
                f.has_child("bytes");
    if (forms != 1) {
        refuse("find: gives exactly one of 'floats', 'words' and 'bytes'");
    }
    std::vector<uint8_t> b, m;
    if (f.has_child("bytes")) {
        std::string s = str(f["bytes"]);
        size_t i = 0;
        while (i < s.size()) {
            if (s[i] == ' ') {
                i++;
                continue;
            }
            std::string tok = s.substr(i, 2);
            if (tok.size() < 2) {
                refuse("find: bytes: a byte is two hex digits or '?" "?'");
            }
            if (tok == "??") {
                b.push_back(0);
                m.push_back(0);
            } else {
                char *end = nullptr;
                unsigned long v = std::strtoul(tok.c_str(), &end, 16);
                if (*end || !std::isxdigit((unsigned char)tok[0])) {
                    refuse("find: bytes: '" + tok + "' is not a byte");
                }
                b.push_back((uint8_t)v);
                m.push_back(0xFF);
            }
            i += 2;
        }
    } else {
        bool fl = f.has_child("floats");
        for (ryml::ConstNodeRef e : f[fl ? "floats" : "words"].children()) {
            uint32_t u = fl ? bits(f32(e, "find: floats")) : u32(e, "find: words");
            for (int k = 0; k < 4; k++) {
                b.push_back((uint8_t)(u >> (8 * k)));
                m.push_back(0xFF);
            }
        }
    }
    if (b.empty() || b.size() > VR_MOD_FIND_MAX) {
        refuse("find: a pattern is 1 to 256 bytes");
    }
    out->len = (int)b.size();
    std::memcpy(out->bytes, b.data(), b.size());
    std::memcpy(out->mask, m.data(), m.size());
    out->align = 4;
    if (f.has_child("align")) {
        uint32_t a = small(f["align"], "find: align");
        if (a != 1 && a != 2 && a != 4) {
            refuse("find: align is 1, 2 or 4");
        }
        out->align = (int)a;
    }
    out->lo = 0;
    out->hi = 0;
    if (f.has_child("range")) {
        ryml::ConstNodeRef r = f["range"];
        if (!r.is_seq() || r.num_children() != 2) {
            refuse("find: range is [lo, hi]");
        }
        out->lo = u32(r[0], "find: range lo");
        out->hi = u32(r[1], "find: range hi");
        if (out->hi <= out->lo) {
            refuse("find: range needs lo < hi");
        }
    }
}

uint32_t read_at(ryml::ConstNodeRef n, bool find, const char *what)
{
    bool a = n.has_child("address"), o = n.has_child("offset");
    if (find ? (!o || a) : (!a || o)) {
        refuse(std::string(what) + (find ? " under find: says 'offset' "
                                           "(and not 'address')"
                                         : " says 'address' (and 'offset' "
                                           "only under find:)"));
    }
    return u32(n[find ? "offset" : "address"], find ? "offset" : "address");
}

struct Name {
    std::string id, slug, version, file;
};

Name parse_name(const std::string &path)
{
    static const std::regex file_re(
        "^([a-z0-9]+(-[a-z0-9]+)*)--v([0-9][0-9a-z.]*)\\.yaml$");
    static const std::regex id_re("^[0-9A-Fa-f]{8}$");
    size_t s = path.find_last_of("/\\");
    std::string file = s == std::string::npos ? path : path.substr(s + 1);
    std::string dir = s == std::string::npos ? "" : path.substr(0, s);
    size_t t = dir.find_last_of("/\\");
    std::string id = t == std::string::npos ? dir : dir.substr(t + 1);
    std::smatch m;
    if (!std::regex_match(file, m, file_re)) {
        refuse("the file name is <slug>--v<version>.yaml (the version is "
               "always in the name)");
    }
    if (!std::regex_match(id, id_re)) {
        refuse("the folder is the XBE title id, 8 hex digits: '" + id + "'");
    }
    return { id, m[1].str(), m[3].str(), file };
}

bool same_id(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower((unsigned char)a[i]) !=
            std::tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

[[noreturn]] void ryml_error(const std::string &what)
{
    throw Refused("YAML: " + what);
}

ryml::Callbacks callbacks()
{
    ryml::Callbacks cb;
    cb.set_error_basic([](ryml::csubstr msg, const ryml::ErrorDataBasic &,
                          void *) { ryml_error(std::string(msg.str, msg.len)); });
    cb.set_error_parse([](ryml::csubstr msg, const ryml::ErrorDataParse &,
                          void *) { ryml_error(std::string(msg.str, msg.len)); });
    cb.set_error_visit([](ryml::csubstr msg, const ryml::ErrorDataVisit &,
                          void *) { ryml_error(std::string(msg.str, msg.len)); });
    return cb;
}

int parse(const std::string &path, const std::string &text, VrMod *out,
          std::string *why)
{
    Name nm = parse_name(path);
    ryml::Callbacks cb = callbacks();
    ryml::EventHandlerTree handler(cb);
    ryml::Parser parser(&handler);
    ryml::Tree tree(cb);
    ryml::parse_in_arena(&parser, ryml::to_csubstr(path),
                         ryml::csubstr(text.data(), text.size()), &tree);
    ryml::ConstNodeRef root = tree.crootref();
    if (!root.is_map() || root.num_children() != 1) {
        refuse("one top key, the title id");
    }
    ryml::ConstNodeRef top = root.first_child();
    std::string key(top.key().str, top.key().len);
    if (!same_id(key, nm.id)) {
        refuse("the top key '" + key + "' is not the folder's title id " +
               nm.id);
    }
    ryml::ConstNodeRef e = top;
    if (top.is_seq()) {
        if (top.num_children() != 1) {
            refuse("one entry per file");
        }
        e = top[0];
    }
    if (!e.is_map()) {
        refuse("the entry is a map");
    }
    std::string kind = e.has_child("kind") ? str(e["kind"]) : "";
    if (kind != "fix" && kind != "conversion") {
        refuse("kind is 'fix' or 'conversion'");
    }
    std::string version = e.has_child("version") ? str(e["version"]) : "";
    if (version != nm.version) {
        refuse("version '" + version + "' is not the file name's '" +
               nm.version + "'");
    }
    if (kind == "conversion") {
        *why = "a conversion: valid, not applied by this core";
        return 0;
    }
    for (const char *ps2 : { "ee", "vu1", "crcs" }) {
        if (e.has_child(ryml::to_csubstr(ps2))) {
            refuse(std::string("'") + ps2 + "' is a PS2 field; an Xbox fix "
                   "says 'ram' and 'xbe_versions'");
        }
    }
    if (e.has_child("origin")) {
        ryml::ConstNodeRef o = e["origin"];
        if (!o.is_map() || !o.has_child("name") || !o.has_child("by")) {
            refuse("origin: names the tweak ('name') and its authors ('by')");
        }
    }

    VrMod m = {};
    m.title_id = (uint32_t)std::strtoul(nm.id.c_str(), nullptr, 16);
    m.enabled = true;
    if (e.has_child("enabled")) {
        std::string en = str(e["enabled"]);
        for (char &c : en) {
            c = (char)std::tolower((unsigned char)c);
        }
        m.enabled = en != "false";
    }
    std::vector<uint32_t> vers;
    if (e.has_child("xbe_versions")) {
        for (ryml::ConstNodeRef v : e["xbe_versions"].children()) {
            vers.push_back(u32(v, "xbe_versions"));
        }
    }
    m.has_find = e.has_child("find");
    if (m.has_find) {
        read_find(e["find"], &m.find);
    }
    std::vector<VrModWord> words;
    if (e.has_child("ram")) {
        for (ryml::ConstNodeRef w : e["ram"].children()) {
            VrModWord wd = {};
            wd.at = read_at(w, m.has_find, "a ram word");
            if (wd.at % 4) {
                refuse("a ram word's address or offset is 4-aligned");
            }
            read_value(w, &wd.value, &wd.original, &wd.has_original);
            if (m.has_find && !wd.has_original) {
                refuse("a ram word under find: carries 'original'");
            }
            words.push_back(wd);
        }
    }
    if (words.empty()) {
        refuse("a fix writes at least one 'ram' word");
    }
    std::vector<VrModGuard> guards;
    if (e.has_child("guards")) {
        for (ryml::ConstNodeRef g : e["guards"].children()) {
            VrModGuard gd = {};
            gd.at = read_at(g, m.has_find, "a guard");
            if (!g.has_child("equals")) {
                refuse("a guard says 'equals'");
            }
            gd.equals = u32(g["equals"], "equals");
            uint32_t w = g.has_child("width") ? small(g["width"], "width") : 4;
            if (w != 1 && w != 2 && w != 4) {
                refuse("a guard's width is 1, 2 or 4");
            }
            if (gd.at % w) {
                refuse("a guard's address or offset is aligned to its width");
            }
            gd.width = (uint8_t)w;
            guards.push_back(gd);
        }
    }

    m.name = dup(e.has_child("name") && !str(e["name"]).empty()
                     ? str(e["name"]) : std::string("(unnamed)"));
    m.slug = dup(nm.slug);
    m.version = dup(nm.version);
    m.source = dup(path);
    m.n_xbe_versions = (int)vers.size();
    m.xbe_versions = (uint32_t *)std::calloc(vers.size() + 1, 4);
    std::memcpy(m.xbe_versions, vers.data(), vers.size() * 4);
    m.n_words = (int)words.size();
    m.words = (VrModWord *)std::calloc(words.size(), sizeof(VrModWord));
    std::memcpy(m.words, words.data(), words.size() * sizeof(VrModWord));
    m.n_guards = (int)guards.size();
    m.guards = (VrModGuard *)std::calloc(guards.size() + 1, sizeof(VrModGuard));
    std::memcpy(m.guards, guards.data(), guards.size() * sizeof(VrModGuard));
    *out = m;
    return 1;
}

std::vector<std::string> list(const std::string &dir, bool want_dirs)
{
    std::vector<std::string> out;
    DIR *d = opendir(dir.c_str());
    if (!d) {
        return out;
    }
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") {
            continue;
        }
        struct stat st;
        if (stat((dir + "/" + n).c_str(), &st) == 0 &&
            S_ISDIR(st.st_mode) == want_dirs) {
            out.push_back(n);
        }
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool read_text(const std::string &path, std::string *out)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    char buf[4096];
    size_t n;
    out->clear();
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out->append(buf, n);
    }
    std::fclose(f);
    return true;
}

}

extern "C" int vr_mods_parse_file(const char *path, const char *text,
                                  size_t len, VrMod *out, char *why,
                                  size_t why_len)
{
    std::string w;
    int r;
    try {
        r = parse(path, std::string(text, len), out, &w);
    } catch (const std::exception &ex) {
        w = ex.what();
        r = -1;
    }
    if (why && why_len) {
        std::snprintf(why, why_len, "%s", w.c_str());
    }
    return r;
}

extern "C" int vr_mods_load(const char *user_dir, const char *shipped_dir,
                            VrMod **out)
{
    std::vector<std::pair<std::string, std::string>> files;
    std::set<std::string> seen;
    auto scan = [&](const std::string &root, bool user) {
        for (const std::string &id : list(root, true)) {
            for (const std::string &f : list(root + "/" + id, false)) {
                if (f.size() < 5 || f.compare(f.size() - 5, 5, ".yaml")) {
                    continue;
                }
                std::string key = id + "/" + f;
                for (char &c : key) {
                    c = (char)std::toupper((unsigned char)c);
                }
                if (seen.count(key)) {
                    std::fprintf(stderr, "vr-mods: %s/%s/%s is hidden by the "
                                 "user's file of the same name\n",
                                 root.c_str(), id.c_str(), f.c_str());
                    continue;
                }
                if (user) {
                    seen.insert(key);
                }
                files.push_back({ key, root + "/" + id + "/" + f });
            }
        }
    };
    if (user_dir && *user_dir) {
        scan(user_dir, true);
    }
    if (shipped_dir && *shipped_dir) {
        scan(std::string(shipped_dir) + "/public", false);
        scan(std::string(shipped_dir) + "/private", false);
    }
    std::vector<VrMod> mods;
    for (const auto &kf : files) {
        std::string text;
        char why[512];
        VrMod m;
        if (!read_text(kf.second, &text)) {
            std::fprintf(stderr, "vr-mods: %s: cannot be read\n",
                         kf.second.c_str());
            continue;
        }
        int r = vr_mods_parse_file(kf.second.c_str(), text.data(), text.size(),
                                   &m, why, sizeof(why));
        if (r < 0) {
            std::fprintf(stderr, "vr-mods: %s: refused: %s\n",
                         kf.second.c_str(), why);
        } else if (r == 0) {
            std::fprintf(stderr, "vr-mods: %s: %s\n", kf.second.c_str(), why);
        } else {
            mods.push_back(m);
        }
    }
    *out = mods.empty() ? nullptr
                        : (VrMod *)std::malloc(mods.size() * sizeof(VrMod));
    if (!mods.empty()) {
        std::memcpy(*out, mods.data(), mods.size() * sizeof(VrMod));
    }
    std::fprintf(stderr, "vr-mods: %zu fix(es) read (user %s, shipped %s)\n",
                 mods.size(), user_dir && *user_dir ? user_dir : "-",
                 shipped_dir && *shipped_dir ? shipped_dir : "-");
    return (int)mods.size();
}
