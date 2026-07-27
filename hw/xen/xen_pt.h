/*
 * Copyright (c) 2007, Neocleus Corporation.
 * Copyright (c) 2007, Intel Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Alex Novik <alex@neocleus.com>
 * Allen Kay <allen.m.kay@intel.com>
 * Guy Zana <guy@neocleus.com>
 */
#ifndef XEN_PT_H
#define XEN_PT_H

#include "hw/xen/xen_native.h"
#include "xen-host-pci-device.h"
#include "qom/object.h"

void xen_pt_log(const PCIDevice *d, const char *f, ...) G_GNUC_PRINTF(2, 3);

#define XEN_PT_ERR(d, _f, _a...) xen_pt_log(d, "%s: Error: "_f, __func__, ##_a)

#ifdef XEN_PT_LOGGING_ENABLED
#  define XEN_PT_LOG(d, _f, _a...)  xen_pt_log(d, "%s: " _f, __func__, ##_a)
#  define XEN_PT_WARN(d, _f, _a...) \
    xen_pt_log(d, "%s: Warning: "_f, __func__, ##_a)
#else
#  define XEN_PT_LOG(d, _f, _a...)
#  define XEN_PT_WARN(d, _f, _a...)
#endif

#ifdef XEN_PT_DEBUG_PCI_CONFIG_ACCESS
#  define XEN_PT_LOG_CONFIG(d, addr, val, len) \
    xen_pt_log(d, "%s: address=0x%04x val=0x%08x len=%d\n", \
               __func__, addr, val, len)
#else
#  define XEN_PT_LOG_CONFIG(d, addr, val, len)
#endif


#define XEN_PFN(x) ((x) >> XC_PAGE_SHIFT)

typedef const struct XenPTRegInfo XenPTRegInfo;
typedef struct XenPTReg XenPTReg;


#define TYPE_XEN_PT_DEVICE "xen-pci-passthrough"
OBJECT_DECLARE_SIMPLE_TYPE(XenPCIPassthroughState, XEN_PT_DEVICE)

#define XEN_PT_DEVICE_CLASS(klass) \
    OBJECT_CLASS_CHECK(XenPTDeviceClass, klass, TYPE_XEN_PT_DEVICE)
#define XEN_PT_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(XenPTDeviceClass, obj, TYPE_XEN_PT_DEVICE)

typedef void (*XenPTQdevRealize)(DeviceState *qdev, Error **errp);

typedef struct XenPTDeviceClass {
    PCIDeviceClass parent_class;
    XenPTQdevRealize pci_qdev_realize;
} XenPTDeviceClass;

typedef int (*xen_pt_conf_reg_init)
    (XenPCIPassthroughState *, XenPTRegInfo *, uint32_t real_offset,
     uint32_t *data);
typedef int (*xen_pt_conf_dword_write)
    (XenPCIPassthroughState *, XenPTReg *cfg_entry,
     uint32_t *val, uint32_t dev_value, uint32_t valid_mask);
typedef int (*xen_pt_conf_word_write)
    (XenPCIPassthroughState *, XenPTReg *cfg_entry,
     uint16_t *val, uint16_t dev_value, uint16_t valid_mask);
typedef int (*xen_pt_conf_byte_write)
    (XenPCIPassthroughState *, XenPTReg *cfg_entry,
     uint8_t *val, uint8_t dev_value, uint8_t valid_mask);
typedef int (*xen_pt_conf_dword_read)
    (XenPCIPassthroughState *, XenPTReg *cfg_entry,
     uint32_t *val, uint32_t valid_mask);
typedef int (*xen_pt_conf_word_read)
    (XenPCIPassthroughState *, XenPTReg *cfg_entry,
     uint16_t *val, uint16_t valid_mask);
typedef int (*xen_pt_conf_byte_read)
    (XenPCIPassthroughState *, XenPTReg *cfg_entry,
     uint8_t *val, uint8_t valid_mask);

#define XEN_PT_BAR_ALLF 0xFFFFFFFF
#define XEN_PT_BAR_UNMAPPED (-1)

#define XEN_PCI_CAP_MAX 48

#define XEN_PCI_INTEL_OPREGION 0xfc

#define XEN_PCI_IGD_DOMAIN 0
#define XEN_PCI_IGD_BUS 0
#define XEN_PCI_IGD_DEV 2
#define XEN_PCI_IGD_FN 0
#define XEN_PCI_IGD_SLOT_MASK \
    (1UL << PCI_SLOT(PCI_DEVFN(XEN_PCI_IGD_DEV, XEN_PCI_IGD_FN)))

typedef enum {
    XEN_PT_GRP_TYPE_HARDWIRED = 0,
    XEN_PT_GRP_TYPE_EMU,
} XenPTRegisterGroupType;

typedef enum {
    XEN_PT_BAR_FLAG_MEM = 0,
    XEN_PT_BAR_FLAG_IO,
    XEN_PT_BAR_FLAG_UPPER,
    XEN_PT_BAR_FLAG_UNUSED,
} XenPTBarFlag;


typedef struct XenPTRegion {
    XenPTBarFlag bar_flag;
    union {
        uint64_t maddr;
        uint64_t pio_base;
        uint64_t u;
    } access;
} XenPTRegion;

struct XenPTRegInfo {
    uint32_t offset;
    uint32_t size;
    uint32_t init_val;
    uint32_t res_mask;
    uint32_t ro_mask;
    uint32_t rw1c_mask;
    uint32_t emu_mask;
    xen_pt_conf_reg_init init;
    union {
        struct {
            xen_pt_conf_dword_write write;
            xen_pt_conf_dword_read read;
        } dw;
        struct {
            xen_pt_conf_word_write write;
            xen_pt_conf_word_read read;
        } w;
        struct {
            xen_pt_conf_byte_write write;
            xen_pt_conf_byte_read read;
        } b;
    } u;
};

struct XenPTReg {
    QLIST_ENTRY(XenPTReg) entries;
    XenPTRegInfo *reg;
    union {
        uint8_t *byte;
        uint16_t *half_word;
        uint32_t *word;
    } ptr;
};

typedef const struct XenPTRegGroupInfo XenPTRegGroupInfo;

typedef int (*xen_pt_reg_size_init_fn)
    (XenPCIPassthroughState *, XenPTRegGroupInfo *,
     uint32_t base_offset, uint8_t *size);

struct XenPTRegGroupInfo {
    uint8_t grp_id;
    XenPTRegisterGroupType grp_type;
    uint8_t grp_size;
    xen_pt_reg_size_init_fn size_init;
    XenPTRegInfo *emu_regs;
};

typedef struct XenPTRegGroup {
    QLIST_ENTRY(XenPTRegGroup) entries;
    XenPTRegGroupInfo *reg_grp;
    uint32_t base_offset;
    uint8_t size;
    QLIST_HEAD(, XenPTReg) reg_tbl_list;
} XenPTRegGroup;


#define XEN_PT_UNASSIGNED_PIRQ (-1)
typedef struct XenPTMSI {
    uint16_t flags;
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint16_t data;
    uint32_t ctrl_offset;
    uint32_t mask;
    int pirq;
    bool initialized;
    bool mapped;
} XenPTMSI;

typedef struct XenPTMSIXEntry {
    int pirq;
    uint64_t addr;
    uint32_t data;
    uint32_t latch[4];
    bool updated;
} XenPTMSIXEntry;
typedef struct XenPTMSIX {
    uint32_t ctrl_offset;
    bool enabled;
    bool maskall;
    int total_entries;
    int bar_index;
    uint64_t table_base;
    uint32_t table_offset_adjust;
    uint64_t mmio_base_addr;
    MemoryRegion mmio;
    void *phys_iomem_base;
    XenPTMSIXEntry msix_entry[];
} XenPTMSIX;

struct XenPCIPassthroughState {
    PCIDevice dev;

    PCIHostDeviceAddress hostaddr;
    bool is_virtfn;
    bool permissive;
    bool permissive_warned;
    XenHostPCIDevice real_device;
    XenPTRegion bases[PCI_NUM_REGIONS];
    QLIST_HEAD(, XenPTRegGroup) reg_grps;

    uint32_t machine_irq;

    XenPTMSI *msi;
    XenPTMSIX *msix;

    MemoryRegion bar[PCI_NUM_REGIONS - 1];
    MemoryRegion rom;

    MemoryListener memory_listener;
    MemoryListener io_listener;
    bool listener_set;
};

void xen_pt_config_init(XenPCIPassthroughState *s, Error **errp);
void xen_pt_config_delete(XenPCIPassthroughState *s);
XenPTRegGroup *xen_pt_find_reg_grp(XenPCIPassthroughState *s, uint32_t address);
XenPTReg *xen_pt_find_reg(XenPTRegGroup *reg_grp, uint32_t address);
int xen_pt_bar_offset_to_index(uint32_t offset);

static inline pcibus_t xen_pt_get_emul_size(XenPTBarFlag flag, pcibus_t r_size)
{
    if (flag == XEN_PT_BAR_FLAG_MEM) {
        return (r_size + XC_PAGE_SIZE - 1) & XC_PAGE_MASK;
    } else {
        return r_size;
    }
}

static inline uint8_t xen_pt_pci_read_intx(XenPCIPassthroughState *s)
{
    uint8_t v = 0;
    xen_host_pci_get_byte(&s->real_device, PCI_INTERRUPT_PIN, &v);
    return v;
}

static inline uint8_t xen_pt_pci_intx(XenPCIPassthroughState *s)
{
    uint8_t r_val = xen_pt_pci_read_intx(s);

    XEN_PT_LOG(&s->dev, "intx=%i\n", r_val);
    if (r_val < 1 || r_val > 4) {
        XEN_PT_LOG(&s->dev, "Interrupt pin read from hardware is out of range:"
                   " value=%i, acceptable range is 1 - 4\n", r_val);
        r_val = 0;
    } else {
        r_val -= 1;
    }

    return r_val;
}

int xen_pt_msi_setup(XenPCIPassthroughState *s);
int xen_pt_msi_update(XenPCIPassthroughState *d);
void xen_pt_msi_disable(XenPCIPassthroughState *s);

int xen_pt_msix_init(XenPCIPassthroughState *s, uint32_t base);
void xen_pt_msix_delete(XenPCIPassthroughState *s);
void xen_pt_msix_unmap(XenPCIPassthroughState *s);
int xen_pt_msix_update(XenPCIPassthroughState *s);
int xen_pt_msix_update_remap(XenPCIPassthroughState *s, int bar_index);
void xen_pt_msix_disable(XenPCIPassthroughState *s);

static inline bool xen_pt_has_msix_mapping(XenPCIPassthroughState *s, int bar)
{
    return s->msix && s->msix->bar_index == bar;
}

void *pci_assign_dev_load_option_rom(PCIDevice *dev, int *size,
                                     unsigned int domain, unsigned int bus,
                                     unsigned int slot, unsigned int function);
int xen_pt_register_vga_regions(XenHostPCIDevice *dev);
int xen_pt_unregister_vga_regions(XenHostPCIDevice *dev);
void xen_pt_setup_vga(XenPCIPassthroughState *s, XenHostPCIDevice *dev,
                     Error **errp);
#endif
