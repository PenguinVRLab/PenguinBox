/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * include/public/trace.h
 *
 * Mark Williamson, (C) 2004 Intel Research Cambridge
 * Copyright (C) 2005 Bin Ren
 */

#ifndef __XEN_PUBLIC_TRACE_H__
#define __XEN_PUBLIC_TRACE_H__

#define TRACE_EXTRA_MAX    7
#define TRACE_EXTRA_SHIFT 28

#define TRC_CLS_SHIFT 16
#define TRC_GEN      0x0001f000
#define TRC_SCHED    0x0002f000
#define TRC_DOM0OP   0x0004f000
#define TRC_HVM      0x0008f000
#define TRC_MEM      0x0010f000
#define TRC_PV       0x0020f000
#define TRC_SHADOW   0x0040f000
#define TRC_HW       0x0080f000
#define TRC_GUEST    0x0800f000
#define TRC_ALL      0x0ffff000
#define TRC_HD_TO_EVENT(x) ((x)&0x0fffffff)
#define TRC_HD_CYCLE_FLAG (1UL<<31)
#define TRC_HD_INCLUDES_CYCLE_COUNT(x) ( !!( (x) & TRC_HD_CYCLE_FLAG ) )
#define TRC_HD_EXTRA(x)    (((x)>>TRACE_EXTRA_SHIFT)&TRACE_EXTRA_MAX)

#define TRC_SUBCLS_SHIFT 12

#define TRC_HVM_ENTRYEXIT   0x00081000
#define TRC_HVM_HANDLER     0x00082000
#define TRC_HVM_EMUL        0x00084000

#define TRC_SCHED_MIN       0x00021000
#define TRC_SCHED_CLASS     0x00022000
#define TRC_SCHED_VERBOSE   0x00028000

#define TRC_SCHED_ID_BITS 3
#define TRC_SCHED_ID_SHIFT (TRC_SUBCLS_SHIFT - TRC_SCHED_ID_BITS)
#define TRC_SCHED_ID_MASK (((1UL<<TRC_SCHED_ID_BITS) - 1) << TRC_SCHED_ID_SHIFT)
#define TRC_SCHED_EVT_MASK (~(TRC_SCHED_ID_MASK))

#define TRC_SCHED_CSCHED   0
#define TRC_SCHED_CSCHED2  1
#define TRC_SCHED_ARINC653 3
#define TRC_SCHED_RTDS     4
#define TRC_SCHED_SNULL    5

#define TRC_SCHED_CLASS_EVT(_c, _e) \
  ( ( TRC_SCHED_CLASS | \
      ((TRC_SCHED_##_c << TRC_SCHED_ID_SHIFT) & TRC_SCHED_ID_MASK) ) + \
    (_e & TRC_SCHED_EVT_MASK) )

#define TRC_DOM0_DOMOPS     0x00041000

#define TRC_HW_PM           0x00801000
#define TRC_HW_IRQ          0x00802000

#define TRC_LOST_RECORDS        (TRC_GEN + 1)
#define TRC_TRACE_WRAP_BUFFER  (TRC_GEN + 2)
#define TRC_TRACE_CPU_CHANGE    (TRC_GEN + 3)

#define TRC_SCHED_RUNSTATE_CHANGE   (TRC_SCHED_MIN + 1)
#define TRC_SCHED_CONTINUE_RUNNING  (TRC_SCHED_MIN + 2)
#define TRC_SCHED_DOM_ADD        (TRC_SCHED_VERBOSE +  1)
#define TRC_SCHED_DOM_REM        (TRC_SCHED_VERBOSE +  2)
#define TRC_SCHED_SLEEP          (TRC_SCHED_VERBOSE +  3)
#define TRC_SCHED_WAKE           (TRC_SCHED_VERBOSE +  4)
#define TRC_SCHED_YIELD          (TRC_SCHED_VERBOSE +  5)
#define TRC_SCHED_BLOCK          (TRC_SCHED_VERBOSE +  6)
#define TRC_SCHED_SHUTDOWN       (TRC_SCHED_VERBOSE +  7)
#define TRC_SCHED_CTL            (TRC_SCHED_VERBOSE +  8)
#define TRC_SCHED_ADJDOM         (TRC_SCHED_VERBOSE +  9)
#define TRC_SCHED_SWITCH         (TRC_SCHED_VERBOSE + 10)
#define TRC_SCHED_S_TIMER_FN     (TRC_SCHED_VERBOSE + 11)
#define TRC_SCHED_T_TIMER_FN     (TRC_SCHED_VERBOSE + 12)
#define TRC_SCHED_DOM_TIMER_FN   (TRC_SCHED_VERBOSE + 13)
#define TRC_SCHED_SWITCH_INFPREV (TRC_SCHED_VERBOSE + 14)
#define TRC_SCHED_SWITCH_INFNEXT (TRC_SCHED_VERBOSE + 15)
#define TRC_SCHED_SHUTDOWN_CODE  (TRC_SCHED_VERBOSE + 16)
#define TRC_SCHED_SWITCH_INFCONT (TRC_SCHED_VERBOSE + 17)

#define TRC_DOM0_DOM_ADD         (TRC_DOM0_DOMOPS + 1)
#define TRC_DOM0_DOM_REM         (TRC_DOM0_DOMOPS + 2)

#define TRC_MEM_PAGE_GRANT_MAP      (TRC_MEM + 1)
#define TRC_MEM_PAGE_GRANT_UNMAP    (TRC_MEM + 2)
#define TRC_MEM_PAGE_GRANT_TRANSFER (TRC_MEM + 3)
#define TRC_MEM_SET_P2M_ENTRY       (TRC_MEM + 4)
#define TRC_MEM_DECREASE_RESERVATION (TRC_MEM + 5)
#define TRC_MEM_POD_POPULATE        (TRC_MEM + 16)
#define TRC_MEM_POD_ZERO_RECLAIM    (TRC_MEM + 17)
#define TRC_MEM_POD_SUPERPAGE_SPLINTER (TRC_MEM + 18)

#define TRC_PV_ENTRY   0x00201000
#define TRC_PV_SUBCALL 0x00202000

#define TRC_PV_HYPERCALL             (TRC_PV_ENTRY +  1)
#define TRC_PV_TRAP                  (TRC_PV_ENTRY +  3)
#define TRC_PV_PAGE_FAULT            (TRC_PV_ENTRY +  4)
#define TRC_PV_FORCED_INVALID_OP     (TRC_PV_ENTRY +  5)
#define TRC_PV_EMULATE_PRIVOP        (TRC_PV_ENTRY +  6)
#define TRC_PV_EMULATE_4GB           (TRC_PV_ENTRY +  7)
#define TRC_PV_MATH_STATE_RESTORE    (TRC_PV_ENTRY +  8)
#define TRC_PV_PAGING_FIXUP          (TRC_PV_ENTRY +  9)
#define TRC_PV_GDT_LDT_MAPPING_FAULT (TRC_PV_ENTRY + 10)
#define TRC_PV_PTWR_EMULATION        (TRC_PV_ENTRY + 11)
#define TRC_PV_PTWR_EMULATION_PAE    (TRC_PV_ENTRY + 12)
#define TRC_PV_HYPERCALL_V2          (TRC_PV_ENTRY + 13)
#define TRC_PV_HYPERCALL_SUBCALL     (TRC_PV_SUBCALL + 14)

#define TRC_PV_HYPERCALL_V2_ARG_32(i) (0x1 << (20 + 2*(i)))
#define TRC_PV_HYPERCALL_V2_ARG_64(i) (0x2 << (20 + 2*(i)))
#define TRC_PV_HYPERCALL_V2_ARG_MASK  (0xfff00000)

#define TRC_SHADOW_NOT_SHADOW                 (TRC_SHADOW +  1)
#define TRC_SHADOW_FAST_PROPAGATE             (TRC_SHADOW +  2)
#define TRC_SHADOW_FAST_MMIO                  (TRC_SHADOW +  3)
#define TRC_SHADOW_FALSE_FAST_PATH            (TRC_SHADOW +  4)
#define TRC_SHADOW_MMIO                       (TRC_SHADOW +  5)
#define TRC_SHADOW_FIXUP                      (TRC_SHADOW +  6)
#define TRC_SHADOW_DOMF_DYING                 (TRC_SHADOW +  7)
#define TRC_SHADOW_EMULATE                    (TRC_SHADOW +  8)
#define TRC_SHADOW_EMULATE_UNSHADOW_USER      (TRC_SHADOW +  9)
#define TRC_SHADOW_EMULATE_UNSHADOW_EVTINJ    (TRC_SHADOW + 10)
#define TRC_SHADOW_EMULATE_UNSHADOW_UNHANDLED (TRC_SHADOW + 11)
#define TRC_SHADOW_WRMAP_BF                   (TRC_SHADOW + 12)
#define TRC_SHADOW_PREALLOC_UNPIN             (TRC_SHADOW + 13)
#define TRC_SHADOW_RESYNC_FULL                (TRC_SHADOW + 14)
#define TRC_SHADOW_RESYNC_ONLY                (TRC_SHADOW + 15)

#define TRC_HVM_NESTEDFLAG      (0x400)
#define TRC_HVM_VMENTRY         (TRC_HVM_ENTRYEXIT + 0x01)
#define TRC_HVM_VMEXIT          (TRC_HVM_ENTRYEXIT + 0x02)
#define TRC_HVM_VMEXIT64        (TRC_HVM_ENTRYEXIT + TRC_64_FLAG + 0x02)
#define TRC_HVM_PF_XEN          (TRC_HVM_HANDLER + 0x01)
#define TRC_HVM_PF_XEN64        (TRC_HVM_HANDLER + TRC_64_FLAG + 0x01)
#define TRC_HVM_PF_INJECT       (TRC_HVM_HANDLER + 0x02)
#define TRC_HVM_PF_INJECT64     (TRC_HVM_HANDLER + TRC_64_FLAG + 0x02)
#define TRC_HVM_INJ_EXC         (TRC_HVM_HANDLER + 0x03)
#define TRC_HVM_INJ_VIRQ        (TRC_HVM_HANDLER + 0x04)
#define TRC_HVM_REINJ_VIRQ      (TRC_HVM_HANDLER + 0x05)
#define TRC_HVM_IO_READ         (TRC_HVM_HANDLER + 0x06)
#define TRC_HVM_IO_WRITE        (TRC_HVM_HANDLER + 0x07)
#define TRC_HVM_CR_READ         (TRC_HVM_HANDLER + 0x08)
#define TRC_HVM_CR_READ64       (TRC_HVM_HANDLER + TRC_64_FLAG + 0x08)
#define TRC_HVM_CR_WRITE        (TRC_HVM_HANDLER + 0x09)
#define TRC_HVM_CR_WRITE64      (TRC_HVM_HANDLER + TRC_64_FLAG + 0x09)
#define TRC_HVM_DR_READ         (TRC_HVM_HANDLER + 0x0A)
#define TRC_HVM_DR_WRITE        (TRC_HVM_HANDLER + 0x0B)
#define TRC_HVM_MSR_READ        (TRC_HVM_HANDLER + 0x0C)
#define TRC_HVM_MSR_WRITE       (TRC_HVM_HANDLER + 0x0D)
#define TRC_HVM_CPUID           (TRC_HVM_HANDLER + 0x0E)
#define TRC_HVM_INTR            (TRC_HVM_HANDLER + 0x0F)
#define TRC_HVM_NMI             (TRC_HVM_HANDLER + 0x10)
#define TRC_HVM_SMI             (TRC_HVM_HANDLER + 0x11)
#define TRC_HVM_VMMCALL         (TRC_HVM_HANDLER + 0x12)
#define TRC_HVM_HLT             (TRC_HVM_HANDLER + 0x13)
#define TRC_HVM_INVLPG          (TRC_HVM_HANDLER + 0x14)
#define TRC_HVM_INVLPG64        (TRC_HVM_HANDLER + TRC_64_FLAG + 0x14)
#define TRC_HVM_MCE             (TRC_HVM_HANDLER + 0x15)
#define TRC_HVM_IOPORT_READ     (TRC_HVM_HANDLER + 0x16)
#define TRC_HVM_IOMEM_READ      (TRC_HVM_HANDLER + 0x17)
#define TRC_HVM_CLTS            (TRC_HVM_HANDLER + 0x18)
#define TRC_HVM_LMSW            (TRC_HVM_HANDLER + 0x19)
#define TRC_HVM_LMSW64          (TRC_HVM_HANDLER + TRC_64_FLAG + 0x19)
#define TRC_HVM_RDTSC           (TRC_HVM_HANDLER + 0x1a)
#define TRC_HVM_INTR_WINDOW     (TRC_HVM_HANDLER + 0x20)
#define TRC_HVM_NPF             (TRC_HVM_HANDLER + 0x21)
#define TRC_HVM_REALMODE_EMULATE (TRC_HVM_HANDLER + 0x22)
#define TRC_HVM_TRAP             (TRC_HVM_HANDLER + 0x23)
#define TRC_HVM_TRAP_DEBUG       (TRC_HVM_HANDLER + 0x24)
#define TRC_HVM_VLAPIC           (TRC_HVM_HANDLER + 0x25)
#define TRC_HVM_XCR_READ64      (TRC_HVM_HANDLER + TRC_64_FLAG + 0x26)
#define TRC_HVM_XCR_WRITE64     (TRC_HVM_HANDLER + TRC_64_FLAG + 0x27)

#define TRC_HVM_IOPORT_WRITE    (TRC_HVM_HANDLER + 0x216)
#define TRC_HVM_IOMEM_WRITE     (TRC_HVM_HANDLER + 0x217)

#define TRC_HVM_EMUL_HPET_START_TIMER  (TRC_HVM_EMUL + 0x1)
#define TRC_HVM_EMUL_PIT_START_TIMER   (TRC_HVM_EMUL + 0x2)
#define TRC_HVM_EMUL_RTC_START_TIMER   (TRC_HVM_EMUL + 0x3)
#define TRC_HVM_EMUL_LAPIC_START_TIMER (TRC_HVM_EMUL + 0x4)
#define TRC_HVM_EMUL_HPET_STOP_TIMER   (TRC_HVM_EMUL + 0x5)
#define TRC_HVM_EMUL_PIT_STOP_TIMER    (TRC_HVM_EMUL + 0x6)
#define TRC_HVM_EMUL_RTC_STOP_TIMER    (TRC_HVM_EMUL + 0x7)
#define TRC_HVM_EMUL_LAPIC_STOP_TIMER  (TRC_HVM_EMUL + 0x8)
#define TRC_HVM_EMUL_PIT_TIMER_CB      (TRC_HVM_EMUL + 0x9)
#define TRC_HVM_EMUL_LAPIC_TIMER_CB    (TRC_HVM_EMUL + 0xA)
#define TRC_HVM_EMUL_PIC_INT_OUTPUT    (TRC_HVM_EMUL + 0xB)
#define TRC_HVM_EMUL_PIC_KICK          (TRC_HVM_EMUL + 0xC)
#define TRC_HVM_EMUL_PIC_INTACK        (TRC_HVM_EMUL + 0xD)
#define TRC_HVM_EMUL_PIC_POSEDGE       (TRC_HVM_EMUL + 0xE)
#define TRC_HVM_EMUL_PIC_NEGEDGE       (TRC_HVM_EMUL + 0xF)
#define TRC_HVM_EMUL_PIC_PEND_IRQ_CALL (TRC_HVM_EMUL + 0x10)
#define TRC_HVM_EMUL_LAPIC_PIC_INTR    (TRC_HVM_EMUL + 0x11)

#define TRC_PM_FREQ_CHANGE      (TRC_HW_PM + 0x01)
#define TRC_PM_IDLE_ENTRY       (TRC_HW_PM + 0x02)
#define TRC_PM_IDLE_EXIT        (TRC_HW_PM + 0x03)

#define TRC_HW_IRQ_MOVE_CLEANUP_DELAY (TRC_HW_IRQ + 0x1)
#define TRC_HW_IRQ_MOVE_CLEANUP       (TRC_HW_IRQ + 0x2)
#define TRC_HW_IRQ_BIND_VECTOR        (TRC_HW_IRQ + 0x3)
#define TRC_HW_IRQ_CLEAR_VECTOR       (TRC_HW_IRQ + 0x4)
#define TRC_HW_IRQ_MOVE_FINISH        (TRC_HW_IRQ + 0x5)
#define TRC_HW_IRQ_ASSIGN_VECTOR      (TRC_HW_IRQ + 0x6)
#define TRC_HW_IRQ_UNMAPPED_VECTOR    (TRC_HW_IRQ + 0x7)
#define TRC_HW_IRQ_HANDLED            (TRC_HW_IRQ + 0x8)

#define TRC_64_FLAG 0x100

struct t_rec {
    uint32_t event:28;
    uint32_t extra_u32:3;
    uint32_t cycles_included:1;
    union {
        struct {
            uint32_t cycles_lo, cycles_hi;
            uint32_t extra_u32[7];
        } cycles;
        struct {
            uint32_t extra_u32[7];
        } nocycles;
    } u;
};

struct t_buf {
    uint32_t cons;
    uint32_t prod;
};

struct t_info {
    uint16_t tbuf_size;
    uint16_t mfn_offset[];
};

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
