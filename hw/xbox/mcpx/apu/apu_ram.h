/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 Patrick Carey
 */
#ifndef HW_XBOX_MCPX_APU_RAM_H
#define HW_XBOX_MCPX_APU_RAM_H

extern int g_apu_ramptr;
void apu_ramptr_check(MCPXAPUState *d, hwaddr addr, int size, uint32_t before, uint32_t got);

static inline uint32_t apu_ld_ram(MCPXAPUState *d, hwaddr addr, int size)
{
    const uint8_t *p = d->ram_ptr + addr;
    switch (size) {
    case 1:
        return ldub_p(p);
    case 2:
        return lduw_le_p(p);
    default:
        return ldl_le_p(p);
    }
}

static inline uint32_t apu_ld_phys_as(hwaddr addr, int size)
{
    switch (size) {
    case 1:
        return ldub_phys(&address_space_memory, addr);
    case 2:
        return lduw_le_phys(&address_space_memory, addr);
    default:
        return ldl_le_phys(&address_space_memory, addr);
    }
}

static inline uint32_t apu_ld_phys_sel(MCPXAPUState *d, hwaddr addr, int size)
{
    if (g_apu_ramptr && addr + size <= d->ram_window) {
        if (unlikely(g_apu_ramptr == 2)) {
            uint32_t before = apu_ld_phys_as(addr, size);
            uint32_t r = apu_ld_ram(d, addr, size);
            apu_ramptr_check(d, addr, size, before, r);
            return r;
        }
        return apu_ld_ram(d, addr, size);
    }
    return apu_ld_phys_as(addr, size);
}

static inline uint32_t apu_ld_phys(MCPXAPUState *d, hwaddr addr, int size)
{
    return apu_ld_phys_sel(d, addr, size);
}

static inline uint32_t apu_ldl_le_phys(MCPXAPUState *d, hwaddr addr)
{
    return apu_ld_phys(d, addr, 4);
}

static inline void apu_stl_le_phys(MCPXAPUState *d, hwaddr addr, uint32_t val)
{
    stl_le_phys(&address_space_memory, addr, val);
}

static inline void apu_stb_phys(MCPXAPUState *d, hwaddr addr, uint32_t val)
{
    stb_phys(&address_space_memory, addr, val);
}

#endif
