/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "vr_mods.h"
#include "vr_mods_watch.h"

int main(int argc, char **argv)
{
    bool watches = argc > 1 && !std::strcmp(argv[1], "--watches");
    if (watches) {
        argv++;
        argc--;
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: vrmods-replay SHIPPED_DIR [TITLE DUMP [OUT]]\n");
        return 2;
    }
    char tmpl[] = "/tmp/vrmods-replay-XXXXXX";
    int fd = mkstemp(tmpl);
    FILE *log = fdopen(fd, "w+");
    FILE *real = fdopen(dup(2), "w");
    dup2(fd, 2);
    VrMod *mods = nullptr;
    int n = vr_mods_load(nullptr, argv[1], &mods);
    fflush(stderr);
    std::string text;
    rewind(log);
    char buf[4096];
    size_t k;
    while ((k = std::fread(buf, 1, sizeof(buf), log)) > 0) {
        text.append(buf, k);
    }
    std::fputs(text.c_str(), real);
    int refused = 0;
    for (size_t p = text.find(": refused:"); p != std::string::npos; p = text.find(": refused:", p + 1)) {
        refused++;
    }
    std::fprintf(real, "replay: %d fix(es) read, %d file(s) refused\n", n, refused);
    int n_files = n;
    if (watches) {
        n = vr_mods_add_watches(&mods, n);
    }
    std::vector<std::string> watch_names;
    for (int i = n_files; i < n; i++) {
        watch_names.push_back(mods[i].slug);
    }
    vr_mods_set_catalogue(mods, n);
    if (argc < 4) {
        unlink(tmpl);
        return refused ? 1 : 0;
    }
    uint32_t title = (uint32_t)std::strtoul(argv[2], nullptr, 16);
    FILE *f = std::fopen(argv[3], "rb");
    if (!f) {
        std::fprintf(real, "replay: cannot read %s\n", argv[3]);
        return 2;
    }
    std::vector<uint8_t> ram;
    while ((k = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        ram.insert(ram.end(), buf, buf + k);
    }
    std::fclose(f);
    std::vector<uint8_t> before = ram;
    for (int i = 0; i < 3; i++) {
        vr_mods_frame(ram.data(), ram.size(), title, -1);
    }
    fflush(stderr);
    text.clear();
    rewind(log);
    while ((k = std::fread(buf, 1, sizeof(buf), log)) > 0) {
        text.append(buf, k);
    }
    std::fputs(text.c_str(), real);
    int changed = 0;
    for (size_t i = 0; i < ram.size(); i += 4) {
        if (std::memcmp(&ram[i], &before[i], 4)) {
            uint32_t a, b;
            std::memcpy(&a, &before[i], 4);
            std::memcpy(&b, &ram[i], 4);
            float fa, fb;
            std::memcpy(&fa, &a, 4);
            std::memcpy(&fb, &b, 4);
            std::fprintf(real, "replay: 0x%08zX  %08X (%g) -> %08X (%g)\n", i, a, fa, b, fb);
            changed++;
        }
    }
    std::fprintf(real, "replay: %d word(s) changed\n", changed);
    for (const std::string &w : watch_names) {
        std::fprintf(real, "replay: watch '%s' %s\n", w.c_str(),
                     vr_mods_armed(w.c_str()) ? "HOLDS" : "does not hold");
    }
    if (argc > 4) {
        FILE *o = std::fopen(argv[4], "wb");
        std::fwrite(ram.data(), 1, ram.size(), o);
        std::fclose(o);
    }
    unlink(tmpl);
    vr_mods_set_catalogue(nullptr, 0);
    return 0;
}
