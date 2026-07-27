/*
 * Generic Virtual-Device Fuzzing Target
 *
 * Copyright Red Hat Inc., 2020
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/range.h"

#include <wordexp.h>

#include "hw/core/cpu.h"
#include "tests/qtest/libqtest.h"
#include "tests/qtest/libqos/pci-pc.h"
#include "fuzz.h"
#include "string.h"
#include "system/memory.h"
#include "system/ramblock.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/boards.h"
#include "generic_fuzz_configs.h"
#include "hw/mem/sparse-mem.h"

static void pci_enum(gpointer pcidev, gpointer bus);

#define SEPARATOR "FUZZ"

enum cmds {
    OP_IN,
    OP_OUT,
    OP_READ,
    OP_WRITE,
    OP_PCI_READ,
    OP_PCI_WRITE,
    OP_DISABLE_PCI,
    OP_ADD_DMA_PATTERN,
    OP_CLEAR_DMA_PATTERNS,
    OP_CLOCK_STEP,
};

#define USEC_IN_SEC 1000000000

#define MAX_DMA_FILL_SIZE 0x10000
#define MAX_TOTAL_DMA_SIZE 0x10000000

#define PCI_HOST_BRIDGE_CFG 0xcf8
#define PCI_HOST_BRIDGE_DATA 0xcfc

typedef struct {
    ram_addr_t addr;
    ram_addr_t size;
} address_range;

static bool qtest_log_enabled;
size_t dma_bytes_written;

MemoryRegion *sparse_mem_mr;

typedef struct {
    uint8_t index;
    uint8_t stride;
    size_t len;
    const uint8_t *data;
} pattern;

static bool avoid_double_fetches;

static QTestState *qts_global;

static GHashTable *fuzzable_memoryregions;
static GPtrArray *fuzzable_pci_devices;

struct get_io_cb_info {
    int index;
    int found;
    address_range result;
};

static bool get_io_address_cb(Int128 start, Int128 size,
                              const MemoryRegion *mr,
                              hwaddr offset_in_region,
                              void *opaque)
{
    struct get_io_cb_info *info = opaque;
    if (g_hash_table_lookup(fuzzable_memoryregions, mr)) {
        if (info->index == 0) {
            info->result.addr = (ram_addr_t)start;
            info->result.size = (ram_addr_t)size;
            info->found = 1;
            return true;
        }
        info->index--;
    }
    return false;
}

static GArray *dma_regions;

static GArray *dma_patterns;
static int dma_pattern_index;
static bool pci_disabled;

static void *pattern_alloc(pattern p, size_t len)
{
    int i;
    uint8_t *buf = g_malloc(len);
    uint8_t sum = 0;

    for (i = 0; i < len; ++i) {
        buf[i] = p.data[i % p.len];
        if ((i % p.len) == p.index) {
            buf[i] += sum;
            sum += p.stride;
        }
    }
    return buf;
}

static int fuzz_memory_access_size(MemoryRegion *mr, unsigned l, hwaddr addr)
{
    unsigned access_size_max = mr->ops->valid.max_access_size;

    if (access_size_max == 0) {
        access_size_max = 4;
    }

    if (!mr->ops->impl.unaligned) {
        unsigned align_size_max = addr & -addr;
        if (align_size_max != 0 && align_size_max < access_size_max) {
            access_size_max = align_size_max;
        }
    }

    if (l > access_size_max) {
        l = access_size_max;
    }
    l = pow2floor(l);

    return l;
}

void fuzz_dma_read_cb(size_t addr, size_t len, MemoryRegion *mr)
{
    if (!qts_global) {
        return;
    }

    if (dma_patterns->len == 0
        || len == 0
        || dma_bytes_written + len > MAX_TOTAL_DMA_SIZE
        || (mr != current_machine->ram && mr != sparse_mem_mr)) {
        return;
    }

    address_range region;
    bool double_fetch = false;
    for (int i = 0;
         i < dma_regions->len && (avoid_double_fetches || qtest_log_enabled);
         ++i) {
        region = g_array_index(dma_regions, address_range, i);
        if (ranges_overlap(addr, len, region.addr, region.size)) {
            double_fetch = true;
            if (addr < region.addr
                && avoid_double_fetches) {
                fuzz_dma_read_cb(addr, region.addr - addr, mr);
            }
            if (addr + len > region.addr + region.size
                && avoid_double_fetches) {
                fuzz_dma_read_cb(region.addr + region.size,
                        addr + len - (region.addr + region.size), mr);
            }
            return;
        }
    }

    len = MIN(len, MAX_DMA_FILL_SIZE);

    address_range ar = {addr, len};
    g_array_append_val(dma_regions, ar);
    pattern p = g_array_index(dma_patterns, pattern, dma_pattern_index);
    void *buf_base = pattern_alloc(p, ar.size);
    void *buf = buf_base;
    hwaddr l, addr1;
    MemoryRegion *mr1;
    while (len > 0) {
        l = len;
        mr1 = address_space_translate(first_cpu->as,
                                      addr, &addr1, &l, true,
                                      MEMTXATTRS_UNSPECIFIED);

        if (!memory_region_is_ram(mr1)) {
            l = fuzz_memory_access_size(mr1, l, addr1);
        }
        if (memory_region_is_ram(mr1) ||
            memory_region_is_romd(mr1) ||
            mr1 == sparse_mem_mr) {
            if (qtest_log_enabled) {
                fprintf(stderr, "[DMA] ");
                if (double_fetch) {
                    fprintf(stderr, "[DOUBLE-FETCH] ");
                }
                fflush(stderr);
            }
            qtest_memwrite(qts_global, addr, buf, l);
            dma_bytes_written += l;
        }
        len -= l;
        buf += l;
        addr += l;

    }
    g_free(buf_base);

    dma_pattern_index = (dma_pattern_index + 1) % dma_patterns->len;
}

static bool get_io_address(address_range *result, AddressSpace *as,
                            uint8_t index,
                            uint32_t offset) {
    FlatView *view;
    view = as->current_map;
    g_assert(view);
    struct get_io_cb_info cb_info = {};

    cb_info.index = index;

    do {
        flatview_for_each_range(view, get_io_address_cb , &cb_info);
    } while (cb_info.index != index && !cb_info.found);

    *result = cb_info.result;
    if (result->size) {
        offset = offset % result->size;
        result->addr += offset;
        result->size -= offset;
    }
    return cb_info.found;
}

static bool get_pio_address(address_range *result,
                            uint8_t index, uint16_t offset)
{
    bool found = get_io_address(result, &address_space_io, index, offset);
    return result->addr <= 0xFFFF ? found : false;
}

static bool get_mmio_address(address_range *result,
                             uint8_t index, uint32_t offset)
{
    return get_io_address(result, &address_space_memory, index, offset);
}

static void op_in(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint16_t offset;
    } a;
    address_range abs;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));
    if (get_pio_address(&abs, a.base, a.offset) == 0) {
        return;
    }

    switch (a.size %= end_sizes) {
    case Byte:
        qtest_inb(s, abs.addr);
        break;
    case Word:
        if (abs.size >= 2) {
            qtest_inw(s, abs.addr);
        }
        break;
    case Long:
        if (abs.size >= 4) {
            qtest_inl(s, abs.addr);
        }
        break;
    }
}

static void op_out(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint16_t offset;
        uint32_t value;
    } a;
    address_range abs;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));

    if (get_pio_address(&abs, a.base, a.offset) == 0) {
        return;
    }

    switch (a.size %= end_sizes) {
    case Byte:
        qtest_outb(s, abs.addr, a.value & 0xFF);
        break;
    case Word:
        if (abs.size >= 2) {
            qtest_outw(s, abs.addr, a.value & 0xFFFF);
        }
        break;
    case Long:
        if (abs.size >= 4) {
            qtest_outl(s, abs.addr, a.value);
        }
        break;
    }
}

static void op_read(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, Quad, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint32_t offset;
    } a;
    address_range abs;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));

    if (get_mmio_address(&abs, a.base, a.offset) == 0) {
        return;
    }

    switch (a.size %= end_sizes) {
    case Byte:
        qtest_readb(s, abs.addr);
        break;
    case Word:
        if (abs.size >= 2) {
            qtest_readw(s, abs.addr);
        }
        break;
    case Long:
        if (abs.size >= 4) {
            qtest_readl(s, abs.addr);
        }
        break;
    case Quad:
        if (abs.size >= 8) {
            qtest_readq(s, abs.addr);
        }
        break;
    }
}

static void op_write(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, Quad, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint32_t offset;
        uint64_t value;
    } a;
    address_range abs;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));

    if (get_mmio_address(&abs, a.base, a.offset) == 0) {
        return;
    }

    switch (a.size %= end_sizes) {
    case Byte:
            qtest_writeb(s, abs.addr, a.value & 0xFF);
        break;
    case Word:
        if (abs.size >= 2) {
            qtest_writew(s, abs.addr, a.value & 0xFFFF);
        }
        break;
    case Long:
        if (abs.size >= 4) {
            qtest_writel(s, abs.addr, a.value & 0xFFFFFFFF);
        }
        break;
    case Quad:
        if (abs.size >= 8) {
            qtest_writeq(s, abs.addr, a.value);
        }
        break;
    }
}

static void op_pci_read(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint8_t offset;
    } a;
    if (len < sizeof(a) || fuzzable_pci_devices->len == 0 || pci_disabled) {
        return;
    }
    memcpy(&a, data, sizeof(a));
    PCIDevice *dev = g_ptr_array_index(fuzzable_pci_devices,
                                  a.base % fuzzable_pci_devices->len);
    int devfn = dev->devfn;
    qtest_outl(s, PCI_HOST_BRIDGE_CFG, (1U << 31) | (devfn << 8) | a.offset);
    switch (a.size %= end_sizes) {
    case Byte:
        qtest_inb(s, PCI_HOST_BRIDGE_DATA);
        break;
    case Word:
        qtest_inw(s, PCI_HOST_BRIDGE_DATA);
        break;
    case Long:
        qtest_inl(s, PCI_HOST_BRIDGE_DATA);
        break;
    }
}

static void op_pci_write(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint8_t offset;
        uint32_t value;
    } a;
    if (len < sizeof(a) || fuzzable_pci_devices->len == 0 || pci_disabled) {
        return;
    }
    memcpy(&a, data, sizeof(a));
    PCIDevice *dev = g_ptr_array_index(fuzzable_pci_devices,
                                  a.base % fuzzable_pci_devices->len);
    int devfn = dev->devfn;
    qtest_outl(s, PCI_HOST_BRIDGE_CFG, (1U << 31) | (devfn << 8) | a.offset);
    switch (a.size %= end_sizes) {
    case Byte:
        qtest_outb(s, PCI_HOST_BRIDGE_DATA, a.value & 0xFF);
        break;
    case Word:
        qtest_outw(s, PCI_HOST_BRIDGE_DATA, a.value & 0xFFFF);
        break;
    case Long:
        qtest_outl(s, PCI_HOST_BRIDGE_DATA, a.value & 0xFFFFFFFF);
        break;
    }
}

static void op_add_dma_pattern(QTestState *s,
                               const unsigned char *data, size_t len)
{
    struct {
        uint8_t index;
        uint8_t stride;
    } a;

    if (len < sizeof(a) + 1) {
        return;
    }
    memcpy(&a, data, sizeof(a));
    pattern p = {a.index, a.stride, len - sizeof(a), data + sizeof(a)};
    p.index = a.index % p.len;
    g_array_append_val(dma_patterns, p);
}

static void op_clear_dma_patterns(QTestState *s,
                                  const unsigned char *data, size_t len)
{
    g_array_set_size(dma_patterns, 0);
    dma_pattern_index = 0;
}

static void op_clock_step(QTestState *s, const unsigned char *data, size_t len)
{
    qtest_clock_step_next(s);
}

static void op_disable_pci(QTestState *s, const unsigned char *data, size_t len)
{
    pci_disabled = true;
}

static void generic_fuzz(QTestState *s, const unsigned char *Data, size_t Size)
{
    void (*ops[]) (QTestState *s, const unsigned char* , size_t) = {
        [OP_IN]                 = op_in,
        [OP_OUT]                = op_out,
        [OP_READ]               = op_read,
        [OP_WRITE]              = op_write,
        [OP_PCI_READ]           = op_pci_read,
        [OP_PCI_WRITE]          = op_pci_write,
        [OP_DISABLE_PCI]        = op_disable_pci,
        [OP_ADD_DMA_PATTERN]    = op_add_dma_pattern,
        [OP_CLEAR_DMA_PATTERNS] = op_clear_dma_patterns,
        [OP_CLOCK_STEP]         = op_clock_step,
    };
    const unsigned char *cmd = Data;
    const unsigned char *nextcmd;
    size_t cmd_len;
    uint8_t op;

    op_clear_dma_patterns(s, NULL, 0);
    pci_disabled = false;
    dma_bytes_written = 0;

    QPCIBus *pcibus = qpci_new_pc(s, NULL);
    g_ptr_array_foreach(fuzzable_pci_devices, pci_enum, pcibus);
    qpci_free_pc(pcibus);

    while (cmd && Size) {
        nextcmd = memmem(cmd, Size, SEPARATOR, strlen(SEPARATOR));
        cmd_len = nextcmd ? nextcmd - cmd : Size;

        if (cmd_len > 0) {
            op = *cmd % (sizeof(ops) / sizeof((ops)[0]));
            ops[op](s, cmd + 1, cmd_len - 1);

            flush_events(s);
        }
        cmd = nextcmd ? nextcmd + sizeof(SEPARATOR) - 1 : nextcmd;
        Size = Size - (cmd_len + sizeof(SEPARATOR) - 1);
        g_array_set_size(dma_regions, 0);
    }
    fuzz_reset(s);
}

static void usage(void)
{
    printf("Please specify the following environment variables:\n");
    printf("QEMU_FUZZ_ARGS= the command line arguments passed to qemu\n");
    printf("QEMU_FUZZ_OBJECTS= "
            "a space separated list of QOM type names for objects to fuzz\n");
    printf("Optionally: QEMU_AVOID_DOUBLE_FETCH= "
            "Try to avoid racy DMA double fetch bugs? %d by default\n",
            avoid_double_fetches);
    exit(0);
}

static int locate_fuzz_memory_regions(Object *child, void *opaque)
{
    MemoryRegion *mr;
    if (object_dynamic_cast(child, TYPE_MEMORY_REGION)) {
        mr = MEMORY_REGION(child);
        if ((memory_region_is_ram(mr) ||
            memory_region_is_ram_device(mr) ||
            memory_region_is_rom(mr)) == false) {
            g_hash_table_insert(fuzzable_memoryregions, mr, (gpointer)true);
        }
    }
    return 0;
}

static int locate_fuzz_objects(Object *child, void *opaque)
{
    GString *type_name;
    GString *path_name;
    char *pattern = opaque;

    type_name = g_string_new(object_get_typename(child));
    g_string_ascii_down(type_name);
    if (g_pattern_match_simple(pattern, type_name->str)) {
        object_child_foreach_recursive(child, locate_fuzz_memory_regions, NULL);

        if (object_dynamic_cast(OBJECT(child), TYPE_PCI_DEVICE)) {
            g_ptr_array_remove_fast(fuzzable_pci_devices, PCI_DEVICE(child));
            g_ptr_array_add(fuzzable_pci_devices, PCI_DEVICE(child));
        }
    } else if (object_dynamic_cast(OBJECT(child), TYPE_MEMORY_REGION)) {
        path_name = g_string_new(object_get_canonical_path_component(child));
        g_string_ascii_down(path_name);
        if (g_pattern_match_simple(pattern, path_name->str)) {
            MemoryRegion *mr;
            mr = MEMORY_REGION(child);
            if ((memory_region_is_ram(mr) ||
                 memory_region_is_ram_device(mr) ||
                 memory_region_is_rom(mr)) == false) {
                g_hash_table_insert(fuzzable_memoryregions, mr, (gpointer)true);
            }
        }
        g_string_free(path_name, true);
    }
    g_string_free(type_name, true);
    return 0;
}


static void pci_enum(gpointer pcidev, gpointer bus)
{
    PCIDevice *dev = pcidev;
    QPCIDevice *qdev;
    int i;

    qdev = qpci_device_find(bus, dev->devfn);
    g_assert(qdev != NULL);
    for (i = 0; i < 6; i++) {
        if (dev->io_regions[i].size) {
            qpci_iomap(qdev, i, NULL);
        }
    }
    qpci_device_enable(qdev);
    g_free(qdev);
}

static void generic_pre_fuzz(QTestState *s)
{
    GHashTableIter iter;
    MemoryRegion *mr;
    char **result;
    GString *name_pattern;

    if (!getenv("QEMU_FUZZ_OBJECTS")) {
        usage();
    }
    if (getenv("QTEST_LOG")) {
        qtest_log_enabled = 1;
    }
    if (getenv("QEMU_AVOID_DOUBLE_FETCH")) {
        avoid_double_fetches = 1;
    }
    qts_global = s;

    sparse_mem_mr = sparse_mem_init(0, UINT64_MAX);

    dma_regions = g_array_new(false, false, sizeof(address_range));
    dma_patterns = g_array_new(false, false, sizeof(pattern));

    fuzzable_memoryregions = g_hash_table_new(NULL, NULL);
    fuzzable_pci_devices   = g_ptr_array_new();

    result = g_strsplit(getenv("QEMU_FUZZ_OBJECTS"), " ", -1);
    for (int i = 0; result[i] != NULL; i++) {
        name_pattern = g_string_new(result[i]);
        g_string_ascii_down(name_pattern);
        printf("Matching objects by name %s\n", result[i]);
        object_child_foreach_recursive(qdev_get_machine(),
                                    locate_fuzz_objects,
                                    name_pattern->str);
        g_string_free(name_pattern, true);
    }
    g_strfreev(result);
    printf("This process will try to fuzz the following MemoryRegions:\n");

    g_hash_table_iter_init(&iter, fuzzable_memoryregions);
    while (g_hash_table_iter_next(&iter, (gpointer)&mr, NULL)) {
        printf("  * %s (size 0x%" PRIx64 ")\n",
               object_get_canonical_path_component(&(mr->parent_obj)),
               memory_region_size(mr));
    }

    if (!g_hash_table_size(fuzzable_memoryregions)) {
        printf("No fuzzable memory regions found...\n");
        exit(1);
    }
}

static size_t generic_fuzz_crossover(const uint8_t *data1, size_t size1, const
                                     uint8_t *data2, size_t size2, uint8_t *out,
                                     size_t max_out_size, unsigned int seed)
{
    size_t copy_len = 0, size = 0;

    if (max_out_size <= size1 + strlen(SEPARATOR) * 3 + 2) {
        return 0;
    }

    copy_len = size1;
    memcpy(out + size, data1, copy_len);
    size += copy_len;
    max_out_size -= copy_len;

    copy_len = strlen(SEPARATOR);
    memcpy(out + size, SEPARATOR, copy_len);
    size += copy_len;
    max_out_size -= copy_len;

    copy_len = 1;
    if (copy_len) {
        out[size] = OP_CLEAR_DMA_PATTERNS;
    }
    size += copy_len;
    max_out_size -= copy_len;

    copy_len = strlen(SEPARATOR);
    memcpy(out + size, SEPARATOR, copy_len);
    size += copy_len;
    max_out_size -= copy_len;

    copy_len = 1;
    if (copy_len) {
        out[size] = OP_DISABLE_PCI;
    }
    size += copy_len;
    max_out_size -= copy_len;

    copy_len = strlen(SEPARATOR);
    memcpy(out + size, SEPARATOR, copy_len);
    size += copy_len;
    max_out_size -= copy_len;

    copy_len = MIN(size2, max_out_size);
    memcpy(out + size, data2, copy_len);
    size += copy_len;
    max_out_size -= copy_len;

    return  size;
}


static GString *generic_fuzz_cmdline(FuzzTarget *t)
{
    GString *cmd_line = g_string_new(TARGET_NAME);
    if (!getenv("QEMU_FUZZ_ARGS")) {
        usage();
    }
    g_string_append_printf(cmd_line, " -display none \
                                      -machine accel=qtest, \
                                      -m 512M %s ", getenv("QEMU_FUZZ_ARGS"));
    return cmd_line;
}

static GString *generic_fuzz_predefined_config_cmdline(FuzzTarget *t)
{
    gchar *args;
    const generic_fuzz_config *config;
    g_assert(t->opaque);

    config = t->opaque;
    g_setenv("QEMU_AVOID_DOUBLE_FETCH", "1", 1);
    if (config->argfunc) {
        args = config->argfunc();
        g_setenv("QEMU_FUZZ_ARGS", args, 1);
        g_free(args);
    } else {
        g_assert_nonnull(config->args);
        g_setenv("QEMU_FUZZ_ARGS", config->args, 1);
    }
    g_setenv("QEMU_FUZZ_OBJECTS", config->objects, 1);
    return generic_fuzz_cmdline(t);
}

static void register_generic_fuzz_targets(void)
{
    fuzz_add_target(&(FuzzTarget){
            .name = "generic-fuzz",
            .description = "Fuzz based on any qemu command-line args. ",
            .get_init_cmdline = generic_fuzz_cmdline,
            .pre_fuzz = generic_pre_fuzz,
            .fuzz = generic_fuzz,
            .crossover = generic_fuzz_crossover
    });

    for (int i = 0; i < ARRAY_SIZE(predefined_configs); i++) {
        const generic_fuzz_config *config = predefined_configs + i;
        fuzz_add_target(&(FuzzTarget){
                .name = g_strconcat("generic-fuzz-", config->name, NULL),
                .description = "Predefined generic-fuzz config.",
                .get_init_cmdline = generic_fuzz_predefined_config_cmdline,
                .pre_fuzz = generic_pre_fuzz,
                .fuzz = generic_fuzz,
                .crossover = generic_fuzz_crossover,
                .opaque = (void *)config
        });
    }
}

fuzz_target_init(register_generic_fuzz_targets);
