/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * arch-x86/xen.h
 *
 * Guest OS interface to x86 Xen.
 *
 * Copyright (c) 2004-2006, K A Fraser
 */

#include "../xen.h"

#ifndef __XEN_PUBLIC_ARCH_X86_XEN_H__
#define __XEN_PUBLIC_ARCH_X86_XEN_H__

#if __XEN_INTERFACE_VERSION__ >= 0x00030201
#define ___DEFINE_XEN_GUEST_HANDLE(name, type) \
    typedef struct { type *p; } __guest_handle_ ## name
#else
#define ___DEFINE_XEN_GUEST_HANDLE(name, type) \
    typedef type * __guest_handle_ ## name
#endif

#define __DEFINE_XEN_GUEST_HANDLE(name, type) \
    ___DEFINE_XEN_GUEST_HANDLE(name, type);   \
    ___DEFINE_XEN_GUEST_HANDLE(const_##name, const type)
#define DEFINE_XEN_GUEST_HANDLE(name)   __DEFINE_XEN_GUEST_HANDLE(name, name)
#define __XEN_GUEST_HANDLE(name)        __guest_handle_ ## name
#define XEN_GUEST_HANDLE(name)          __XEN_GUEST_HANDLE(name)
#define XEN_GUEST_HANDLE_PARAM(name)    XEN_GUEST_HANDLE(name)
#define set_xen_guest_handle_raw(hnd, val)  do { (hnd).p = val; } while (0)
#define set_xen_guest_handle(hnd, val) set_xen_guest_handle_raw(hnd, val)

#if defined(__i386__)
# ifdef __XEN__
__DeFiNe__ __DECL_REG_LO8(which) uint32_t e ## which ## x
__DeFiNe__ __DECL_REG_LO16(name) union { uint32_t e ## name; }
# endif
#include "xen-x86_32.h"
# ifdef __XEN__
__UnDeF__ __DECL_REG_LO8
__UnDeF__ __DECL_REG_LO16
__DeFiNe__ __DECL_REG_LO8(which) e ## which ## x
__DeFiNe__ __DECL_REG_LO16(name) e ## name
# endif
#elif defined(__x86_64__)
#include "xen-x86_64.h"
#endif

#ifndef __ASSEMBLY__
typedef unsigned long xen_pfn_t;
#define PRI_xen_pfn "lx"
#define PRIu_xen_pfn "lu"
#endif

#define XEN_HAVE_PV_GUEST_ENTRY 1

#define XEN_HAVE_PV_UPCALL_MASK 1

#define FIRST_RESERVED_GDT_PAGE  14
#define FIRST_RESERVED_GDT_BYTE  (FIRST_RESERVED_GDT_PAGE * 4096)
#define FIRST_RESERVED_GDT_ENTRY (FIRST_RESERVED_GDT_BYTE / 8)


#define XEN_LEGACY_MAX_VCPUS 32

#ifndef __ASSEMBLY__

typedef unsigned long xen_ulong_t;
#define PRI_xen_ulong "lx"

#define TI_GET_DPL(_ti)      ((_ti)->flags & 3)
#define TI_GET_IF(_ti)       ((_ti)->flags & 4)
#define TI_SET_DPL(_ti,_dpl) ((_ti)->flags |= (_dpl))
#define TI_SET_IF(_ti,_if)   ((_ti)->flags |= ((!!(_if))<<2))
struct trap_info {
    uint8_t       vector;
    uint8_t       flags;
    uint16_t      cs;
    unsigned long address;
};
typedef struct trap_info trap_info_t;
DEFINE_XEN_GUEST_HANDLE(trap_info_t);

typedef uint64_t tsc_timestamp_t;

struct vcpu_guest_context {
    struct { char x[512]; } fpu_ctxt;
#define VGCF_I387_VALID                (1<<0)
#define VGCF_IN_KERNEL                 (1<<2)
#define _VGCF_i387_valid               0
#define VGCF_i387_valid                (1<<_VGCF_i387_valid)
#define _VGCF_in_kernel                2
#define VGCF_in_kernel                 (1<<_VGCF_in_kernel)
#define _VGCF_failsafe_disables_events 3
#define VGCF_failsafe_disables_events  (1<<_VGCF_failsafe_disables_events)
#define _VGCF_syscall_disables_events  4
#define VGCF_syscall_disables_events   (1<<_VGCF_syscall_disables_events)
#define _VGCF_online                   5
#define VGCF_online                    (1<<_VGCF_online)
    unsigned long flags;
    struct cpu_user_regs user_regs;
    struct trap_info trap_ctxt[256];
    unsigned long ldt_base, ldt_ents;
    unsigned long gdt_frames[16], gdt_ents;
    unsigned long kernel_ss, kernel_sp;
    unsigned long ctrlreg[8];
    unsigned long debugreg[8];
#ifdef __i386__
    unsigned long event_callback_cs;
    unsigned long event_callback_eip;
    unsigned long failsafe_callback_cs;
    unsigned long failsafe_callback_eip;
#else
    unsigned long event_callback_eip;
    unsigned long failsafe_callback_eip;
#ifdef __XEN__
    union {
        unsigned long syscall_callback_eip;
        struct {
            unsigned int event_callback_cs;
            unsigned int failsafe_callback_cs;
        };
    };
#else
    unsigned long syscall_callback_eip;
#endif
#endif
    unsigned long vm_assist;
#ifdef __x86_64__
    uint64_t      fs_base;
    uint64_t      gs_base_kernel;
    uint64_t      gs_base_user;
#endif
};
typedef struct vcpu_guest_context vcpu_guest_context_t;
DEFINE_XEN_GUEST_HANDLE(vcpu_guest_context_t);

struct arch_shared_info {
    unsigned long max_pfn;
    xen_pfn_t     pfn_to_mfn_frame_list_list;
    unsigned long nmi_reason;
    unsigned long p2m_cr3;
    unsigned long p2m_vaddr;
    unsigned long p2m_generation;
#ifdef __i386__
    uint32_t wc_sec_hi;
#endif
};
typedef struct arch_shared_info arch_shared_info_t;

#if defined(__XEN__) || defined(__XEN_TOOLS__)
struct xen_arch_domainconfig {
#define _XEN_X86_EMU_LAPIC          0
#define XEN_X86_EMU_LAPIC           (1U<<_XEN_X86_EMU_LAPIC)
#define _XEN_X86_EMU_HPET           1
#define XEN_X86_EMU_HPET            (1U<<_XEN_X86_EMU_HPET)
#define _XEN_X86_EMU_PM             2
#define XEN_X86_EMU_PM              (1U<<_XEN_X86_EMU_PM)
#define _XEN_X86_EMU_RTC            3
#define XEN_X86_EMU_RTC             (1U<<_XEN_X86_EMU_RTC)
#define _XEN_X86_EMU_IOAPIC         4
#define XEN_X86_EMU_IOAPIC          (1U<<_XEN_X86_EMU_IOAPIC)
#define _XEN_X86_EMU_PIC            5
#define XEN_X86_EMU_PIC             (1U<<_XEN_X86_EMU_PIC)
#define _XEN_X86_EMU_VGA            6
#define XEN_X86_EMU_VGA             (1U<<_XEN_X86_EMU_VGA)
#define _XEN_X86_EMU_IOMMU          7
#define XEN_X86_EMU_IOMMU           (1U<<_XEN_X86_EMU_IOMMU)
#define _XEN_X86_EMU_PIT            8
#define XEN_X86_EMU_PIT             (1U<<_XEN_X86_EMU_PIT)
#define _XEN_X86_EMU_USE_PIRQ       9
#define XEN_X86_EMU_USE_PIRQ        (1U<<_XEN_X86_EMU_USE_PIRQ)
#define _XEN_X86_EMU_VPCI           10
#define XEN_X86_EMU_VPCI            (1U<<_XEN_X86_EMU_VPCI)

#define XEN_X86_EMU_ALL             (XEN_X86_EMU_LAPIC | XEN_X86_EMU_HPET |  \
                                     XEN_X86_EMU_PM | XEN_X86_EMU_RTC |      \
                                     XEN_X86_EMU_IOAPIC | XEN_X86_EMU_PIC |  \
                                     XEN_X86_EMU_VGA | XEN_X86_EMU_IOMMU |   \
                                     XEN_X86_EMU_PIT | XEN_X86_EMU_USE_PIRQ |\
                                     XEN_X86_EMU_VPCI)
    uint32_t emulation_flags;

#define XEN_X86_MSR_RELAXED (1u << 0)
    uint32_t misc_flags;
};

#define XEN_X86_MISC_FLAGS_MAX XEN_X86_MSR_RELAXED

#endif

typedef struct xen_cpuid_leaf {
#define XEN_CPUID_NO_SUBLEAF 0xffffffffu
    uint32_t leaf, subleaf;
    uint32_t a, b, c, d;
} xen_cpuid_leaf_t;
DEFINE_XEN_GUEST_HANDLE(xen_cpuid_leaf_t);

typedef struct xen_msr_entry {
    uint32_t idx;
    uint32_t flags;
    uint64_t val;
} xen_msr_entry_t;
DEFINE_XEN_GUEST_HANDLE(xen_msr_entry_t);

#endif

#ifdef __ASSEMBLY__
#define XEN_EMULATE_PREFIX .byte 0x0f,0x0b,0x78,0x65,0x6e ;
#define XEN_CPUID          XEN_EMULATE_PREFIX cpuid
#else
#define XEN_EMULATE_PREFIX ".byte 0x0f,0x0b,0x78,0x65,0x6e ; "
#define XEN_CPUID          XEN_EMULATE_PREFIX "cpuid"
#endif

#define XEN_HVM_DEBUGCONS_IOPORT 0xe9

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
