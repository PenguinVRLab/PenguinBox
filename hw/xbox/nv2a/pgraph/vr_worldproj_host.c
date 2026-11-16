/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "exec/cpu-common.h"
#include "hw/core/cpu.h"
#include "ui/xemu-settings.h"
#include "vr_worldproj_host.h"
#include "vr_mods.h"

#define XBE_MAGIC            0x48454258u
#define XBE_HDR_VADDR        0x00010000u
#define XBE_HDR_CERT_OFF     0x118u
#define XBE_CERT_TITLEID_OFF 0x8u
#define XBE_CERT_VERSION_OFF 0xACu

static int64_t host_title_id = -1;
static int64_t host_title_version = -1;
static int host_pending;

bool vr_wp_host_view_projection(void)
{
    return g_config.vr.enable && g_config.vr.view == CONFIG_VR_VIEW_PROJECTION;
}

static bool host_read_u32(CPUState *cs, uint32_t vaddr, uint32_t *out)
{
    uint8_t b[4];

    if (cpu_memory_rw_debug(cs, vaddr, b, sizeof(b), false) != 0) {
        return false;
    }
    *out = (uint32_t)ldl_le_p(b);
    return true;
}

static void host_title_cb(CPUState *cs, run_on_cpu_data data)
{
    uint32_t magic, cert, title, version;
    int64_t id = -1, ver = -1;

    if (host_read_u32(cs, XBE_HDR_VADDR, &magic) && magic == XBE_MAGIC &&
        host_read_u32(cs, XBE_HDR_VADDR + XBE_HDR_CERT_OFF, &cert) && cert &&
        host_read_u32(cs, cert + XBE_CERT_TITLEID_OFF, &title)) {
        id = title;
        if (host_read_u32(cs, cert + XBE_CERT_VERSION_OFF, &version)) {
            ver = version;
        }
    }
    __atomic_store_n(&host_title_version, ver, __ATOMIC_RELEASE);
    __atomic_store_n(&host_title_id, id, __ATOMIC_RELEASE);
    __atomic_store_n(&host_pending, 0, __ATOMIC_RELEASE);
}

void vr_wp_host_title_request(bool forget)
{
    CPUState *cs;

    if (forget) {
        __atomic_store_n(&host_title_id, -1, __ATOMIC_RELEASE);
        __atomic_store_n(&host_title_version, -1, __ATOMIC_RELEASE);
    }
    if (__atomic_exchange_n(&host_pending, 1, __ATOMIC_ACQ_REL)) {
        return;
    }
    cs = qemu_get_cpu(0);
    if (!cs) {
        __atomic_store_n(&host_pending, 0, __ATOMIC_RELEASE);
        return;
    }
    async_safe_run_on_cpu(cs, host_title_cb, RUN_ON_CPU_NULL);
}

int64_t vr_wp_host_title_id(void)
{
    return __atomic_load_n(&host_title_id, __ATOMIC_ACQUIRE);
}

int64_t vr_wp_host_title_version(void)
{
    return __atomic_load_n(&host_title_version, __ATOMIC_ACQUIRE);
}

bool vr_wp_host_mod_armed(const char *slug)
{
    return vr_mods_armed(slug);
}
