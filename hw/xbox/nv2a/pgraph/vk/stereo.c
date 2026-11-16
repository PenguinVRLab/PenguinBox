/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "renderer.h"

static int stereo_requested = -1;
static bool stereo_on;

bool pgraph_vk_stereo_requested(void)
{
    if (stereo_requested < 0) {
        const char *v = getenv("XEMU_VR_STEREO_LAYERS");
        stereo_requested = v && v[0] && strcmp(v, "0") != 0;
    }
    return stereo_requested;
}


bool pgraph_vk_stereo_on(void)
{
    return stereo_on;
}

bool pgraph_vk_stereo_device_features(bool multiview, bool multiview_gs)
{
    stereo_on = false;
    if (!pgraph_vk_stereo_requested()) {
        return false;
    }
    if (multiview && multiview_gs) {
        fprintf(stderr,
                "vr-stereo: multiview present (multiview 1, "
                "multiviewGeometryShader 1), both enabled: two-layer surfaces "
                "and multiview passes ON (stage 2: both views identical; "
                "nothing shown changes)\n");
        stereo_on = true;
        pgraph_vk_stereo_init();
    } else {
        fprintf(stderr,
                "vr-stereo: REFUSED: XEMU_VR_STEREO_LAYERS is set but the "
                "device lacks multiview (multiview %d, multiviewGeometryShader "
                "%d; both are needed): stereo stays OFF, running mono\n",
                multiview, multiview_gs);
    }
    return stereo_on;
}

#include "hw/xbox/nv2a/pgraph/vr_stereo_promo.h"
#include "hw/xbox/nv2a/pgraph/vr_worldproj.h"
#include "vr_xr_eyes.h"

#define STEREO_SUMMARY_FLIPS 600

static VrStereoPromo promo;
static struct {
    unsigned int flips;
    unsigned int transitions;
    unsigned int recreated;
    unsigned int twin_copies;
    uint64_t twin_bytes;
    unsigned int mixed_draws;
    unsigned int multiview_draws;
    unsigned int fallback_draws;
    unsigned int feedback_draws;
} st;
static unsigned int marks_drawn, marks_skipped;

void pgraph_vk_stereo_init(void)
{
    vr_sp_init(&promo, VR_SP_DECAY_DEFAULT);
    memset(&st, 0, sizeof(st));
}

unsigned int pgraph_vk_stereo_layers_for(const SurfaceBinding *target)
{
    if (!stereo_on) {
        return 1;
    }
    return vr_sp_promoted(&promo, target->vram_addr, target->color) ? 2 : 1;
}

void pgraph_vk_stereo_note_recreate(const SurfaceBinding *old,
                                    unsigned int layers)
{
    st.recreated++;
    fprintf(stderr,
            "vr-stereo: %s 0x%" HWADDR_PRIx " %ux%u recreated with %u layer%s "
            "(was %u)\n",
            old->color ? "colour" : "zeta", old->vram_addr, old->width,
            old->height, layers, layers == 1 ? "" : "s", old->layers);
}

static VrSpSurf sp_surf(const SurfaceBinding *s)
{
    return (VrSpSurf){ s->vram_addr, s->width, s->height, s->color };
}

void pgraph_vk_stereo_note_draw(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;
    SurfaceBinding *c = r->color_binding, *z = r->zeta_binding;

    if (c && z && c->layers != z->layers) {
        st.mixed_draws++;
    }
    if (pgraph_vk_stereo_view_mask(r)) {
        st.multiview_draws++;
    } else if ((c && c->layers > 1) || (z && z->layers > 1)) {
        st.fallback_draws++;
        if ((c && c->feedback_mode) || (z && z->feedback_mode)) {
            st.feedback_draws++;
        }
    }
    if (!c) {
        return;
    }
    VrSpSurf cs = sp_surf(c), zs = { 0 };
    VrSpSurf src[NV2A_MAX_TEXTURES];
    int n = 0;
    if (z) {
        zs = sp_surf(z);
    }
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        if (!pgraph_is_texture_enabled(pg, i)) {
            continue;
        }
        SurfaceBinding *s =
            pgraph_vk_surface_get(d, pgraph_get_texture_phys_addr(pg, i));
        if (s) {
            src[n++] = sp_surf(s);
        }
    }
    vr_sp_note_draw(&promo, &cs, z ? &zs : NULL, src, n);
}

static int mirror = -1;
static const char *mirror_path;
static bool mirror_changed;
static unsigned int mirror_presents_l1, mirror_presents_mono;

static int mirror_read(void)
{
    FILE *f = fopen(mirror_path, "r");
    int ch = f ? fgetc(f) : EOF;
    if (f) {
        fclose(f);
    }
    return ch == '1';
}

static void mirror_init(void)
{
    const char *v = getenv("XEMU_VR_STEREO_MIRROR");
    if (v && v[0] && strcmp(v, "0") != 0 && strcmp(v, "1") != 0) {
        mirror_path = v;
        mirror = mirror_read();
        fprintf(stderr, "vr-stereo: mirror %d: the window shows layer %d of "
                "a two-layer presented surface (watch file %s, read every "
                "flip)\n", mirror, mirror, mirror_path);
        return;
    }
    mirror = v && strcmp(v, "1") == 0;
    fprintf(stderr, "vr-stereo: mirror %d: the window shows layer %d of "
            "a two-layer presented surface\n", mirror, mirror);
}

static void mirror_poll(void)
{
    if (mirror < 0) {
        mirror_init();
    }
    if (!mirror_path) {
        return;
    }
    int m = mirror_read();
    if (m != mirror) {
        mirror = m;
        mirror_changed = true;
        fprintf(stderr, "vr-stereo: flip %u: mirror %d (watch file): the "
                "window shows layer %d\n", st.flips, mirror, mirror);
    }
}

bool pgraph_vk_stereo_mirror_changed(void)
{
    bool c = mirror_changed;
    mirror_changed = false;
    return c;
}

VkImageView pgraph_vk_stereo_display_view(SurfaceBinding *s)
{
    if (mirror < 0) {
        mirror_init();
    }
    if (!mirror) {
        return s->image_view;
    }
    if (s->layers > 1 && s->image_view_layer1 != VK_NULL_HANDLE) {
        mirror_presents_l1++;
        return s->image_view_layer1;
    }
    mirror_presents_mono++;
    return s->image_view;
}

VkImageView pgraph_vk_stereo_eye_view(SurfaceBinding *s, unsigned int layer)
{
    if (layer == 1 && s->layers > 1 && s->image_view_layer1 != VK_NULL_HANDLE) {
        return s->image_view_layer1;
    }
    return s->image_view;
}


uint8_t pgraph_vk_stereo_view_mask(const PGRAPHVkState *r)
{
    if (!stereo_on) {
        return 0;
    }
    const SurfaceBinding *c = r->color_binding, *z = r->zeta_binding;
    if ((!c && !z) || (c && c->layers < 2) || (z && z->layers < 2)) {
        return 0;
    }
    if ((c && c->feedback_mode) || (z && z->feedback_mode)) {
        return 0;
    }
    return 0x3;
}

void pgraph_vk_stereo_render_pass_begun(PGRAPHVkState *r)
{
    if (r->framebuffer_view_mask) {
        return;
    }
    if (r->color_binding && r->color_binding->layers > 1) {
        r->color_binding->twin_dirty = true;
    }
    if (r->zeta_binding && r->zeta_binding->layers > 1) {
        r->zeta_binding->twin_dirty = true;
    }
}

static void twin_copy(PGRAPHState *pg, VkCommandBuffer cmd, SurfaceBinding *s)
{
    unsigned int w = s->width ? s->width : 1, h = s->height ? s->height : 1;
    pgraph_apply_scaling_factor(pg, &w, &h);
    VkImageLayout at = s->image_layout;
    VkImageAspectFlags aspect = s->host_fmt.aspect;
    const VkAccessFlags any = VK_ACCESS_MEMORY_READ_BIT |
                              VK_ACCESS_MEMORY_WRITE_BIT;

    VkImageMemoryBarrier to[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = any,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = at,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s->image,
            .subresourceRange = { aspect, 0, 1, 0, 1 },
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = any,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = at,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s->image,
            .subresourceRange = { aspect, 0, 1, 1, 1 },
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                         2, to);

    VkImageCopy region = {
        .srcSubresource = { aspect, 0, 0, 1 },
        .dstSubresource = { aspect, 0, 1, 1 },
        .extent = { w, h, 1 },
    };
    vkCmdCopyImage(cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   s->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier back[2] = { to[0], to[1] };
    back[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    back[0].dstAccessMask = any;
    back[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back[0].newLayout = at;
    back[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    back[1].dstAccessMask = any;
    back[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    back[1].newLayout = at;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                         NULL, 2, back);

    st.twin_copies++;
    st.twin_bytes += (uint64_t)w * h * s->host_fmt.host_bytes_per_pixel;
}

static const char *why_name(VrSpWhy why)
{
    switch (why) {
    case VR_SP_SEED:
        return "seed";
    case VR_SP_SAMPLED:
        return "sampled by a promoted target, covering it";
    case VR_SP_ZETA:
        return "zeta beside a promoted colour target";
    }
    return "?";
}

static void summary(NV2AState *d)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;
    VrSpEntry list[16];
    char line[512];
    int n = vr_sp_list(&promo, list, 16), k = 0, live = 0;

    for (int i = 0; i < n && i < 16; i++) {
        k += snprintf(line + k, sizeof(line) - k, " %s:0x%llx",
                      list[i].color ? "c" : "z",
                      (unsigned long long)list[i].addr);
    }
    SurfaceBinding *s;
    QTAILQ_FOREACH(s, &r->surfaces, entry) {
        live += s->layers > 1;
    }
    fprintf(stderr,
            "vr-stereo: flip %u: promoted%s%s; live two-layer surfaces %d; over "
            "%d flips: transitions %u, recreated %u, draws in multiview passes "
            "%u, in mono-fallback passes %u (mixed-layer %u, feedback-held "
            "%u), repair copies %u (%.1f MB); mirror presents from layer 1 "
            "%u, of a one-layer surface %u; marks drawn %u, skipped %u; "
            "observations dropped (ever) %u\n",
            st.flips, n ? line : " none", n > 16 ? " (+ more)" : "", live,
            STEREO_SUMMARY_FLIPS, st.transitions, st.recreated,
            st.multiview_draws, st.fallback_draws, st.mixed_draws,
            st.feedback_draws, st.twin_copies, st.twin_bytes / 1e6,
            mirror_presents_l1, mirror_presents_mono, marks_drawn,
            marks_skipped, promo.overflow);
    mirror_presents_l1 = mirror_presents_mono = 0;
    marks_drawn = marks_skipped = 0;
    st.transitions = st.recreated = st.twin_copies = st.mixed_draws = 0;
    st.multiview_draws = st.fallback_draws = st.feedback_draws = 0;
    st.twin_bytes = 0;
}

void pgraph_vk_stereo_flip_begin(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (!stereo_on) {
        return;
    }
    st.flips++;
    mirror_poll();

    uint64_t seeds[2];
    int ns = vr_sp_seeds(vr_worldproj_flip_key(), vr_worldproj_last_present(),
                         seeds);
    VrSpChange ch[16];
    int nc = vr_sp_flip(&promo, seeds, ns, ch, 16);
    for (int i = 0; i < nc && i < 16; i++) {
        fprintf(stderr, "vr-stereo: flip %u: %s %s 0x%llx%s%s\n", st.flips,
                ch[i].promoted ? "promote" : "demote",
                ch[i].color ? "colour" : "zeta",
                (unsigned long long)ch[i].addr, ch[i].promoted ? ": " : "",
                ch[i].promoted ? why_name(ch[i].why) : "");
    }
    if (nc > 16) {
        fprintf(stderr, "vr-stereo: flip %u: + %d more transitions\n",
                st.flips, nc - 16);
    }
    st.transitions += nc;

    bool repair = true;
    if (repair) {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        SurfaceBinding *s;
        QTAILQ_FOREACH(s, &r->surfaces, entry) {
            if (s->layers < 2 || !s->twin_dirty) {
                continue;
            }
            if (cmd == VK_NULL_HANDLE) {
                cmd = pgraph_vk_begin_nondraw_commands(pg);
            }
            twin_copy(pg, cmd, s);
            s->twin_dirty = false;
            s->last_write_submit = r->submit_count;
        }
        if (cmd != VK_NULL_HANDLE) {
            pgraph_vk_end_nondraw_commands(pg, cmd);
        }
    }

    if (st.flips % STEREO_SUMMARY_FLIPS == 0) {
        summary(d);
    }
}



void pgraph_vk_stereo_fold_query_results(PGRAPHVkState *r, uint32_t base,
                                         uint64_t *results, uint32_t n)
{
    vr_sp_fold_pairs(r->query_slot_views, base, results, n, NULL);
}


void pgraph_vk_stereo_finalize_display(PGRAPHState *pg)
{
}

