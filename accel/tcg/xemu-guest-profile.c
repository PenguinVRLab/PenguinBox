/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#ifdef XBOX

#include "qemu/atomic.h"
#include "exec/xemu-guest-profile.h"

bool xemu_gp_enabled;
static uint32_t gp_interval = 4096;
static uint32_t gp_dump_flips = 300;
static const char *gp_path = "guest-profile.bin";

typedef struct GpSample {
    uint32_t eip;
    uint32_t idx;
} GpSample;

#define GP_RING_BITS 20
#define GP_RING_SIZE (1u << GP_RING_BITS)
#define GP_RING_MASK (GP_RING_SIZE - 1u)

static GpSample gp_ring[GP_RING_SIZE];
static uint64_t gp_head;
static uint32_t gp_counter;
static uint32_t gp_sample_idx;

static uint32_t gp_flip_count;
static int gp_dump_request;
static unsigned gp_dump_seq;

static void xemu_gp_dump(void)
{
    uint64_t total = gp_head;
    uint64_t count = total < GP_RING_SIZE ? total : GP_RING_SIZE;
    g_autofree char *tmp = NULL;
    FILE *fp;

    if (count == 0) {
        return;
    }

    tmp = g_strdup_printf("%s.tmp", gp_path);
    fp = fopen(tmp, "wb");
    if (!fp) {
        fprintf(stderr, "[guest-profile] cannot open %s: %s\n",
                tmp, strerror(errno));
        return;
    }

    if (total < GP_RING_SIZE) {
        fwrite(gp_ring, sizeof(GpSample), total, fp);
    } else {
        uint32_t head_slot = (uint32_t)(total & GP_RING_MASK);
        fwrite(&gp_ring[head_slot], sizeof(GpSample),
               GP_RING_SIZE - head_slot, fp);
        fwrite(&gp_ring[0], sizeof(GpSample), head_slot, fp);
    }

    fflush(fp);
    fclose(fp);

    if (rename(tmp, gp_path) != 0) {
        fprintf(stderr, "[guest-profile] rename %s -> %s failed: %s\n",
                tmp, gp_path, strerror(errno));
        return;
    }

    fprintf(stderr,
            "[guest-profile] flush #%u: %" PRIu64 " samples (of %" PRIu64
            " total, interval=%u) -> %s\n",
            ++gp_dump_seq, count, total, gp_interval, gp_path);
}

void xemu_guest_profile_tb(uint32_t eip)
{
    if (++gp_counter < gp_interval) {
        return;
    }
    gp_counter = 0;

    gp_ring[gp_head & GP_RING_MASK] = (GpSample){
        .eip = eip,
        .idx = ++gp_sample_idx,
    };
    gp_head++;

    if (unlikely(qatomic_read(&gp_dump_request))) {
        qatomic_set(&gp_dump_request, 0);
        xemu_gp_dump();
    }
}

void xemu_guest_profile_flip(void)
{
    if (!xemu_gp_enabled) {
        return;
    }
    if (++gp_flip_count >= gp_dump_flips) {
        gp_flip_count = 0;
        qatomic_set(&gp_dump_request, 1);
    }
}

static void __attribute__((constructor)) xemu_gp_init(void)
{
    const char *e = getenv("XEMU_GUEST_PROFILE");
    const char *v;
    int n;

    if (!e || !e[0]) {
        return;
    }

    n = atoi(e);
    if (n > 0) {
        gp_interval = (uint32_t)n;
    }

    v = getenv("XEMU_GUEST_PROFILE_DUMP_FLIPS");
    if (v) {
        n = atoi(v);
        if (n > 0) {
            gp_dump_flips = (uint32_t)n;
        }
    }

    v = getenv("XEMU_GUEST_PROFILE_PATH");
    if (v && v[0]) {
        gp_path = v;
    }

    xemu_gp_enabled = true;
    fprintf(stderr,
            "[guest-profile] ARMED: interval=%u TBs, flush every %u flips, "
            "out=%s (chaining forced off while armed)\n",
            gp_interval, gp_dump_flips, gp_path);
}

#endif
