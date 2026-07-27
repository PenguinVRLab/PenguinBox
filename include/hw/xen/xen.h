/*
 * public xen header
 *   stuff needed outside xen-*.c, i.e. interfaces to qemu.
 *   must not depend on any xen headers being present in
 *   /usr/include/xen, so it can be included unconditionally.
 */
#ifndef QEMU_HW_XEN_H
#define QEMU_HW_XEN_H

#ifndef __XEN_INTERFACE_VERSION__
#define __XEN_INTERFACE_VERSION__ 0x00040e00
#endif

enum xen_mode {
    XEN_DISABLED = 0,
    XEN_ATTACH,
    XEN_EMULATE,
};

extern uint32_t xen_domid;
extern enum xen_mode xen_mode;
extern bool xen_domid_restrict;
extern bool xen_is_stubdomain;

int xen_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num);
int xen_set_pci_link_route(uint8_t link, uint8_t irq);
void xen_intx_set_irq(void *opaque, int irq_num, int level);
void xen_hvm_inject_msi(uint64_t addr, uint32_t data);
int xen_is_pirq_msi(uint32_t msi_data);

qemu_irq *xen_interrupt_controller_init(void);

void xen_register_framebuffer(struct MemoryRegion *mr);

#endif
