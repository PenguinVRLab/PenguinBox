/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include <math.h>
#include "qemu/thread.h"
#include "ui/xemu-settings.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "renderer.h"
#include "vr.h"
#include "vr_internal.h"
#include "hw/xbox/nv2a/pgraph/vr_worldproj.h"

#define WRAP_PI(a) \
    ((a) > (float)M_PI  ? (a) - 2.0f * (float)M_PI : \
     (a) < -(float)M_PI ? (a) + 2.0f * (float)M_PI : (a))

static inline float wrap_period(float x, float period)
{
    if (period <= 0.0f) {
        period = 2.0f * (float)M_PI;
    }
    return x - period * roundf(x / period);
}


static bool parse_addr(const char *str, uint64_t ram_size, uint32_t *out)
{
    if (str == NULL || str[0] == '\0') {
        return false;
    }
    char *end = NULL;
    unsigned long long v = strtoull(str, &end, 0);
    if (end == NULL || *end != '\0' || v == 0 || (v & 3) ||
        v + sizeof(float) > ram_size) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static bool parse_gate_addr(const char *str, uint64_t ram_size,
                            uint32_t *out)
{
    if (str == NULL || str[0] == '\0') {
        return false;
    }
    char *end = NULL;
    unsigned long long v = strtoull(str, &end, 0);
    if (end == NULL || *end != '\0' || v == 0 || v >= ram_size) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static void refresh_addrs(VRCamera *cam, uint64_t ram_size)
{
    const char *ys = g_config.vr.headlook_yaw_addr;
    const char *ps = g_config.vr.headlook_pitch_addr;
    const char *gs = g_config.vr.headlook_gate_addr;

    if (g_strcmp0(gs, cam->gate_addr_str) != 0) {
        g_free(cam->gate_addr_str);
        cam->gate_addr_str = g_strdup(gs);
        cam->gate_addr_valid = parse_gate_addr(gs, ram_size,
                                               &cam->gate_addr);
        cam->gate_was_on = false;
        if (gs && gs[0] && !cam->gate_addr_valid) {
            fprintf(stderr, "PenguinBox: headlook_gate_addr '%s' invalid\n",
                    gs);
        }
    }

    if (g_strcmp0(ys, cam->yaw_addr_str) != 0) {
        g_free(cam->yaw_addr_str);
        cam->yaw_addr_str = g_strdup(ys);
        cam->yaw_addr_valid = parse_addr(ys, ram_size, &cam->yaw_addr);
        cam->base_captured = false;
        if (ys && ys[0] && !cam->yaw_addr_valid) {
            fprintf(stderr, "PenguinBox: headlook_yaw_addr '%s' invalid "
                            "(need 4-aligned address < 0x%" PRIx64 ")\n",
                    ys, ram_size);
        }
    }
    if (g_strcmp0(ps, cam->pitch_addr_str) != 0) {
        g_free(cam->pitch_addr_str);
        cam->pitch_addr_str = g_strdup(ps);
        cam->pitch_addr_valid = parse_addr(ps, ram_size, &cam->pitch_addr);
        cam->base_captured = false;
        if (ps && ps[0] && !cam->pitch_addr_valid) {
            fprintf(stderr, "PenguinBox: headlook_pitch_addr '%s' invalid\n",
                    ps);
        }
    }
}

static void parse_aob(VRCamera *cam, const char *s)
{
    cam->aob_len = 0;
    if (s == NULL) {
        return;
    }
    while (*s && cam->aob_len < (int)sizeof(cam->aob_bytes)) {
        while (*s == ' ') {
            s++;
        }
        if (!*s) {
            break;
        }
        if (*s == '?') {
            cam->aob_bytes[cam->aob_len] = 0;
            cam->aob_mask[cam->aob_len] = 0;
            cam->aob_len++;
            s += (s[1] == '?') ? 2 : 1;
        } else {
            char buf[3] = { s[0], s[1] ? s[1] : '\0', '\0' };
            cam->aob_bytes[cam->aob_len] = (uint8_t)strtoul(buf, NULL, 16);
            cam->aob_mask[cam->aob_len] = 0xFF;
            cam->aob_len++;
            s += buf[1] ? 2 : 1;
        }
    }
}

static bool aob_match_at(const VRCamera *cam, const uint8_t *ram, uint64_t off)
{
    for (int i = 0; i < cam->aob_len; i++) {
        if (cam->aob_mask[i] && ram[off + i] != cam->aob_bytes[i]) {
            return false;
        }
    }
    return true;
}

static void refresh_base(VRCamera *cam, uint64_t ram_size)
{
    const char *ms = g_config.vr.headlook_base_mode;
    const char *ps = g_config.vr.headlook_base_ptr;
    const char *as = g_config.vr.headlook_base_aob;

    if (g_strcmp0(ms, cam->base_mode_str) != 0) {
        g_free(cam->base_mode_str);
        cam->base_mode_str = g_strdup(ms);
        cam->base_mode = (ms && !strcmp(ms, "pointer")) ? 1 :
                         (ms && !strcmp(ms, "aob"))     ? 2 : 0;
        cam->base_captured = false;
        cam->aob_resolved = false;
    }
    if (g_strcmp0(ps, cam->base_ptr_str) != 0) {
        g_free(cam->base_ptr_str);
        cam->base_ptr_str = g_strdup(ps);
        cam->base_ptr_valid = parse_addr(ps, ram_size, &cam->base_ptr);
        cam->base_captured = false;
        if (ps && ps[0] && !cam->base_ptr_valid) {
            fprintf(stderr, "PenguinBox: headlook_base_ptr '%s' invalid\n", ps);
        }
    }
    if (g_strcmp0(as, cam->base_aob_str) != 0) {
        g_free(cam->base_aob_str);
        cam->base_aob_str = g_strdup(as);
        parse_aob(cam, as);
        cam->aob_resolved = false;
        cam->base_captured = false;
    }
}

static bool resolve_cam_base(VRCamera *cam, NV2AState *d, uint64_t ram,
                             uint32_t *base)
{
    const int32_t boff = g_config.vr.headlook_base_offset;

    if (cam->base_mode == 0) {
        *base = 0;
        return true;
    }

    if (cam->base_mode == 1) {
        if (!cam->base_ptr_valid || (uint64_t)cam->base_ptr + 4 > ram) {
            return false;
        }
        uint32_t p = *(const uint32_t *)(d->vram_ptr + cam->base_ptr);
        int64_t b = (int64_t)p + boff;
        if (b <= 0 || (b & 3) || (uint64_t)b + 64 > ram) {
            return false;
        }
        int32_t vo = g_config.vr.headlook_base_validate_offset;
        uint32_t ve = (uint32_t)g_config.vr.headlook_base_validate_equals;
        if (vo != 0 || ve != 0) {
            int64_t va = (int64_t)p + vo;
            if (va < 0 || (uint64_t)va + 4 > ram ||
                *(const uint32_t *)(d->vram_ptr + va) != ve) {
                return false;
            }
        }
        *base = (uint32_t)b;
        return true;
    }

    if (cam->aob_len == 0) {
        return false;
    }
    if (cam->aob_resolved) {
        int64_t moff = (int64_t)cam->aob_base - boff;
        if (moff >= 0 && (uint64_t)moff + cam->aob_len <= ram &&
            aob_match_at(cam, d->vram_ptr, (uint64_t)moff)) {
            *base = cam->aob_base;
            return true;
        }
        cam->aob_resolved = false;
    }
    if (cam->aob_throttle > 0) {
        cam->aob_throttle--;
        return false;
    }
    uint64_t s = g_config.vr.headlook_base_scan_start;
    uint64_t e = (uint64_t)g_config.vr.headlook_base_scan_end;
    if (e == 0 || e > ram) {
        e = ram;
    }
    const bool first_wild = (cam->aob_mask[0] == 0);
    const uint8_t first = cam->aob_bytes[0];
    uint64_t off = s;
    while (off + cam->aob_len <= e) {
        if (!first_wild) {
            const uint8_t *hit = memchr(d->vram_ptr + off, first,
                                        e - off - cam->aob_len + 1);
            if (hit == NULL) {
                break;
            }
            off = (uint64_t)(hit - d->vram_ptr);
        }
        if (aob_match_at(cam, d->vram_ptr, off)) {
            int64_t b = (int64_t)off + boff;
            if (b > 0 && (uint64_t)b + 64 <= ram) {
                cam->aob_base = (uint32_t)b;
                cam->aob_resolved = true;
                *base = cam->aob_base;
                fprintf(stderr, "PenguinBox: camera AOB resolved: base 0x%08X\n",
                        cam->aob_base);
                return true;
            }
        }
        off++;
    }
    cam->aob_throttle = 60;
    return false;
}

static void head_angles_rel(const VRCompositor *c, float *yaw, float *pitch)
{
    const XrQuaternionf q = c->head_pose.orientation;
    const float fx = -2.0f * (q.x * q.z + q.w * q.y);
    const float fy = 2.0f * (q.w * q.x - q.y * q.z);
    const float fz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));

    float head_yaw = atan2f(-fx, -fz);
    float head_pitch = asinf(fy < -1.0f ? -1.0f : (fy > 1.0f ? 1.0f : fy));

    const XrQuaternionf rq = c->recenter_pose.orientation;
    float anchor_yaw = atan2f(2.0f * (rq.w * rq.y),
                              1.0f - 2.0f * (rq.y * rq.y));

    *yaw = WRAP_PI(head_yaw - anchor_yaw);
    *pitch = head_pitch;
}


void vr_camera_reset(void)
{
    g_xemu_vr.cam.base_captured = false;
}


static void vr_camera_write(PGRAPHState *pg, float yaw, float pitch,
                            bool ref_zero);

void vr_camera_frame(PGRAPHState *pg)
{
    VRCompositor *c = &g_xemu_vr.comp;

    vr_worldproj_set_slave(false, 0.0, 0.0);

    if (!c->head_pose_valid) {
        return;
    }

    float yaw, pitch;
    head_angles_rel(c, &yaw, &pitch);
    vr_camera_write(pg, yaw, pitch, false);
}


static void vr_camera_write(PGRAPHState *pg, float yaw, float pitch,
                            bool ref_zero)
{
    VRCamera *cam = &g_xemu_vr.cam;
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    const uint64_t ram_size = memory_region_size(d->vram);

    if (!g_config.vr.headlook_enable) {
        cam->base_captured = false;
        return;
    }

    refresh_addrs(cam, ram_size);
    refresh_base(cam, ram_size);
    if (!cam->yaw_addr_valid) {
        return;
    }

    if (cam->gate_addr_valid) {
        const uint8_t gate = *(const uint8_t *)(d->vram_ptr + cam->gate_addr);
        const bool gate_on =
            (gate & (uint8_t)g_config.vr.headlook_gate_mask) ==
            (uint8_t)g_config.vr.headlook_gate_value;
        if (gate_on && !cam->gate_was_on) {
            cam->base_captured = false;
        }
        cam->gate_was_on = gate_on;
        if (!gate_on) {
            return;
        }
    }

    uint32_t cbase;
    if (!resolve_cam_base(cam, d, ram_size, &cbase)) {
        cam->base_captured = false;
        return;
    }
    const uint64_t ya = (uint64_t)cbase + cam->yaw_addr;
    const uint64_t pa = (uint64_t)cbase + cam->pitch_addr;
    if (ya + sizeof(float) > ram_size) {
        return;
    }
    const bool have_pitch =
        cam->pitch_addr_valid && pa + sizeof(float) <= ram_size;
    float *yaw_ptr = (float *)(d->vram_ptr + ya);
    float *pitch_ptr = have_pitch ? (float *)(d->vram_ptr + pa) : NULL;

    if (!cam->base_captured) {
        cam->yaw_base = *yaw_ptr;
        cam->head_ref_yaw = ref_zero ? 0.0f : yaw;
        cam->last_written_yaw = *yaw_ptr;
        if (pitch_ptr) {
            cam->pitch_base = *pitch_ptr;
            cam->head_ref_pitch = ref_zero ? 0.0f : pitch;
            cam->last_written_pitch = *pitch_ptr;
        }
        cam->base_captured = true;
        fprintf(stderr, "PenguinBox: head-look zeroed (game yaw=%f%s, head ref "
                        "%.1f/%.1f deg, base 0x%08X).\n", cam->yaw_base,
                pitch_ptr ? "" : " [pitch off]",
                yaw * (180.0f / (float)M_PI),
                pitch * (180.0f / (float)M_PI), cbase);
    }

    const float sens = g_config.vr.headlook_sensitivity;
    const float sign = g_config.vr.headlook_invert_pitch ? -1.0f : 1.0f;
    const float period = g_config.vr.headlook_engine_period;

    const float game_yaw = *yaw_ptr;
    cam->yaw_base += wrap_period(game_yaw - cam->last_written_yaw, period);
    const float out_yaw = wrap_period(
        cam->yaw_base + WRAP_PI(yaw - cam->head_ref_yaw) * sens, period);
    *yaw_ptr = out_yaw;
    cam->last_written_yaw = out_yaw;

    if (pitch_ptr) {
        const float game_pitch = *pitch_ptr;
        cam->pitch_base += game_pitch - cam->last_written_pitch;
        float out_pitch =
            cam->pitch_base + (pitch - cam->head_ref_pitch) * sens * sign;
        const float pmin = g_config.vr.headlook_pitch_min;
        const float pmax = g_config.vr.headlook_pitch_max;
        if (pmax > pmin) {
            out_pitch = out_pitch < pmin ? pmin :
                        out_pitch > pmax ? pmax : out_pitch;
        }
        *pitch_ptr = out_pitch;
        cam->last_written_pitch = out_pitch;
    }

    {
        const double k = period > 0.0f ? 2.0 * M_PI / period : 1.0;
        vr_worldproj_set_slave(true, cam->yaw_base * k,
                               pitch_ptr ? cam->pitch_base * k : NAN);
    }
}

void vr_camera_shutdown(void)
{
    VRCamera *cam = &g_xemu_vr.cam;
    g_free(cam->yaw_addr_str);
    g_free(cam->pitch_addr_str);
    g_free(cam->gate_addr_str);
    g_free(cam->base_mode_str);
    g_free(cam->base_ptr_str);
    g_free(cam->base_aob_str);
    cam->yaw_addr_str = NULL;
    cam->pitch_addr_str = NULL;
    cam->gate_addr_str = NULL;
    cam->base_mode_str = NULL;
    cam->base_ptr_str = NULL;
    cam->base_aob_str = NULL;
    cam->yaw_addr_valid = false;
    cam->pitch_addr_valid = false;
    cam->gate_addr_valid = false;
    cam->base_ptr_valid = false;
    cam->base_mode = 0;
    cam->aob_resolved = false;
    cam->aob_len = 0;
    cam->gate_was_on = false;
    cam->base_captured = false;
}
