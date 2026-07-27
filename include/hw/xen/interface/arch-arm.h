/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * arch-arm.h
 *
 * Guest OS interface to ARM Xen.
 *
 * Copyright 2011 (C) Citrix Systems
 */

#ifndef __XEN_PUBLIC_ARCH_ARM_H__
#define __XEN_PUBLIC_ARCH_ARM_H__

#define XEN_HYPERCALL_TAG   0XEA1

#define  int64_aligned_t  int64_t __attribute__((aligned(8)))
#define uint64_aligned_t uint64_t __attribute__((aligned(8)))

#ifndef __ASSEMBLY__
#define ___DEFINE_XEN_GUEST_HANDLE(name, type)                  \
    typedef union { type *p; unsigned long q; }                 \
        __guest_handle_ ## name;                                \
    typedef union { type *p; uint64_aligned_t q; }              \
        __guest_handle_64_ ## name

#define __DEFINE_XEN_GUEST_HANDLE(name, type) \
    ___DEFINE_XEN_GUEST_HANDLE(name, type);   \
    ___DEFINE_XEN_GUEST_HANDLE(const_##name, const type)
#define DEFINE_XEN_GUEST_HANDLE(name)   __DEFINE_XEN_GUEST_HANDLE(name, name)
#define __XEN_GUEST_HANDLE(name)        __guest_handle_64_ ## name
#define XEN_GUEST_HANDLE(name)          __XEN_GUEST_HANDLE(name)
#define XEN_GUEST_HANDLE_PARAM(name)    __guest_handle_ ## name
#define set_xen_guest_handle_raw(hnd, val)                  \
    do {                                                    \
        __typeof__(&(hnd)) _sxghr_tmp = &(hnd);             \
        _sxghr_tmp->q = 0;                                  \
        _sxghr_tmp->p = val;                                \
    } while ( 0 )
#define set_xen_guest_handle(hnd, val) set_xen_guest_handle_raw(hnd, val)

typedef uint64_t xen_pfn_t;
#define PRI_xen_pfn PRIx64
#define PRIu_xen_pfn PRIu64

#define XEN_LEGACY_MAX_VCPUS 1

typedef uint64_t xen_ulong_t;
#define PRI_xen_ulong PRIx64

#if defined(__XEN__) || defined(__XEN_TOOLS__)
#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
# define __DECL_REG(n64, n32) union {          \
        uint64_t n64;                          \
        uint32_t n32;                          \
    }
#else
#define __DECL_REG(n64, n32) uint64_t n64
#endif

struct vcpu_guest_core_regs
{
    __DECL_REG(x0,           r0_usr);
    __DECL_REG(x1,           r1_usr);
    __DECL_REG(x2,           r2_usr);
    __DECL_REG(x3,           r3_usr);
    __DECL_REG(x4,           r4_usr);
    __DECL_REG(x5,           r5_usr);
    __DECL_REG(x6,           r6_usr);
    __DECL_REG(x7,           r7_usr);
    __DECL_REG(x8,           r8_usr);
    __DECL_REG(x9,           r9_usr);
    __DECL_REG(x10,          r10_usr);
    __DECL_REG(x11,          r11_usr);
    __DECL_REG(x12,          r12_usr);

    __DECL_REG(x13,          sp_usr);
    __DECL_REG(x14,          lr_usr);

    __DECL_REG(x15,          __unused_sp_hyp);

    __DECL_REG(x16,          lr_irq);
    __DECL_REG(x17,          sp_irq);

    __DECL_REG(x18,          lr_svc);
    __DECL_REG(x19,          sp_svc);

    __DECL_REG(x20,          lr_abt);
    __DECL_REG(x21,          sp_abt);

    __DECL_REG(x22,          lr_und);
    __DECL_REG(x23,          sp_und);

    __DECL_REG(x24,          r8_fiq);
    __DECL_REG(x25,          r9_fiq);
    __DECL_REG(x26,          r10_fiq);
    __DECL_REG(x27,          r11_fiq);
    __DECL_REG(x28,          r12_fiq);

    __DECL_REG(x29,          sp_fiq);
    __DECL_REG(x30,          lr_fiq);

    __DECL_REG(pc64,         pc32);
    uint64_t cpsr;

    union {
        uint64_t spsr_el1;
        uint32_t spsr_svc;
    };

    uint32_t spsr_fiq, spsr_irq, spsr_und, spsr_abt;

    uint64_t sp_el0;
    uint64_t sp_el1, elr_el1;
};
typedef struct vcpu_guest_core_regs vcpu_guest_core_regs_t;
DEFINE_XEN_GUEST_HANDLE(vcpu_guest_core_regs_t);

#undef __DECL_REG

struct vcpu_guest_context {
#define _VGCF_online                   0
#define VGCF_online                    (1<<_VGCF_online)
    uint32_t flags;

    struct vcpu_guest_core_regs user_regs;

    uint64_t sctlr;
    uint64_t ttbcr, ttbr0, ttbr1;
};
typedef struct vcpu_guest_context vcpu_guest_context_t;
DEFINE_XEN_GUEST_HANDLE(vcpu_guest_context_t);

#define XEN_DOMCTL_CONFIG_GIC_NATIVE    0
#define XEN_DOMCTL_CONFIG_GIC_V2        1
#define XEN_DOMCTL_CONFIG_GIC_V3        2

#define XEN_DOMCTL_CONFIG_TEE_NONE      0
#define XEN_DOMCTL_CONFIG_TEE_OPTEE     1

struct xen_arch_domainconfig {
    uint8_t gic_version;
    uint16_t tee_type;
    uint32_t nr_spis;
    uint32_t clock_frequency;
};
#endif

struct arch_vcpu_info {
};
typedef struct arch_vcpu_info arch_vcpu_info_t;

struct arch_shared_info {
};
typedef struct arch_shared_info arch_shared_info_t;
typedef uint64_t xen_callback_t;

#endif

#if defined(__XEN__) || defined(__XEN_TOOLS__)

#define PSR_THUMB       (1<<5)
#define PSR_FIQ_MASK    (1<<6)
#define PSR_IRQ_MASK    (1<<7)
#define PSR_ABT_MASK    (1<<8)
#define PSR_BIG_ENDIAN  (1<<9)
#define PSR_DBG_MASK    (1<<9)
#define PSR_IT_MASK     (0x0600fc00)
#define PSR_JAZELLE     (1<<24)
#define PSR_Z           (1<<30)

#define PSR_MODE_USR 0x10
#define PSR_MODE_FIQ 0x11
#define PSR_MODE_IRQ 0x12
#define PSR_MODE_SVC 0x13
#define PSR_MODE_MON 0x16
#define PSR_MODE_ABT 0x17
#define PSR_MODE_HYP 0x1a
#define PSR_MODE_UND 0x1b
#define PSR_MODE_SYS 0x1f

#define PSR_MODE_BIT  0x10
#define PSR_MODE_EL3h 0x0d
#define PSR_MODE_EL3t 0x0c
#define PSR_MODE_EL2h 0x09
#define PSR_MODE_EL2t 0x08
#define PSR_MODE_EL1h 0x05
#define PSR_MODE_EL1t 0x04
#define PSR_MODE_EL0t 0x00

#define PSR_GUEST32_INIT (PSR_Z|PSR_ABT_MASK|PSR_FIQ_MASK|PSR_IRQ_MASK|PSR_MODE_SVC)
#define PSR_GUEST64_INIT (PSR_ABT_MASK|PSR_FIQ_MASK|PSR_IRQ_MASK|PSR_MODE_EL1h)

#define SCTLR_GUEST_INIT    xen_mk_ullong(0x00c50078)

#define GUEST_VIRTIO_MMIO_BASE   xen_mk_ullong(0x02000000)
#define GUEST_VIRTIO_MMIO_SIZE   xen_mk_ullong(0x00100000)

#define GUEST_GICD_BASE   xen_mk_ullong(0x03001000)
#define GUEST_GICD_SIZE   xen_mk_ullong(0x00001000)
#define GUEST_GICC_BASE   xen_mk_ullong(0x03002000)
#define GUEST_GICC_SIZE   xen_mk_ullong(0x00002000)

#define GUEST_GICV3_GICD_BASE      xen_mk_ullong(0x03001000)
#define GUEST_GICV3_GICD_SIZE      xen_mk_ullong(0x00010000)

#define GUEST_GICV3_RDIST_REGIONS  1

#define GUEST_GICV3_GICR0_BASE     xen_mk_ullong(0x03020000)
#define GUEST_GICV3_GICR0_SIZE     xen_mk_ullong(0x01000000)

#define GUEST_VPCI_ECAM_BASE    xen_mk_ullong(0x10000000)
#define GUEST_VPCI_ECAM_SIZE    xen_mk_ullong(0x10000000)

#define GUEST_ACPI_BASE xen_mk_ullong(0x20000000)
#define GUEST_ACPI_SIZE xen_mk_ullong(0x02000000)

#define GUEST_PL011_BASE    xen_mk_ullong(0x22000000)
#define GUEST_PL011_SIZE    xen_mk_ullong(0x00001000)

#define GUEST_VPCI_ADDR_TYPE_MEM            xen_mk_ullong(0x02000000)
#define GUEST_VPCI_MEM_ADDR                 xen_mk_ullong(0x23000000)
#define GUEST_VPCI_MEM_SIZE                 xen_mk_ullong(0x10000000)

#define GUEST_GNTTAB_BASE xen_mk_ullong(0x38000000)
#define GUEST_GNTTAB_SIZE xen_mk_ullong(0x01000000)

#define GUEST_MAGIC_BASE  xen_mk_ullong(0x39000000)
#define GUEST_MAGIC_SIZE  xen_mk_ullong(0x01000000)

#define GUEST_RAM_BANKS   2

#define GUEST_RAM0_BASE   xen_mk_ullong(0x40000000)
#define GUEST_RAM0_SIZE   xen_mk_ullong(0xc0000000)

#define GUEST_VPCI_ADDR_TYPE_PREFETCH_MEM   xen_mk_ullong(0x42000000)
#define GUEST_VPCI_PREFETCH_MEM_ADDR        xen_mk_ullong(0x100000000)
#define GUEST_VPCI_PREFETCH_MEM_SIZE        xen_mk_ullong(0x100000000)

#define GUEST_RAM1_BASE   xen_mk_ullong(0x0200000000)
#define GUEST_RAM1_SIZE   xen_mk_ullong(0xfe00000000)

#define GUEST_RAM_BASE    GUEST_RAM0_BASE
#define GUEST_RAM_MAX     (GUEST_RAM0_SIZE + GUEST_RAM1_SIZE)
#define GUEST_RAM_BANK_BASES   { GUEST_RAM0_BASE, GUEST_RAM1_BASE }
#define GUEST_RAM_BANK_SIZES   { GUEST_RAM0_SIZE, GUEST_RAM1_SIZE }

#define GUEST_MAX_VCPUS 128

#define GUEST_TIMER_VIRT_PPI    27
#define GUEST_TIMER_PHYS_S_PPI  29
#define GUEST_TIMER_PHYS_NS_PPI 30
#define GUEST_EVTCHN_PPI        31

#define GUEST_VPL011_SPI        32

#define GUEST_VIRTIO_MMIO_SPI_FIRST   33
#define GUEST_VIRTIO_MMIO_SPI_LAST    43

#define PSCI_cpu_suspend 0
#define PSCI_cpu_off     1
#define PSCI_cpu_on      2
#define PSCI_migrate     3

#endif

#ifndef __ASSEMBLY__
typedef struct xen_pmu_arch { uint8_t dummy; } xen_pmu_arch_t;
#endif

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
