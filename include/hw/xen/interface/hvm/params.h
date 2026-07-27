/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2007, Keir Fraser
 */

#ifndef __XEN_PUBLIC_HVM_PARAMS_H__
#define __XEN_PUBLIC_HVM_PARAMS_H__

#include "hvm_op.h"

#if defined(__XEN__) || defined(__XEN_TOOLS__)

#define HVM_PARAM_PAE_ENABLED                4
#define HVM_PARAM_DM_DOMAIN                 13
#define HVM_PARAM_MEMORY_EVENT_CR0          20
#define HVM_PARAM_MEMORY_EVENT_CR3          21
#define HVM_PARAM_MEMORY_EVENT_CR4          22
#define HVM_PARAM_MEMORY_EVENT_INT3         23
#define HVM_PARAM_NESTEDHVM                 24
#define HVM_PARAM_MEMORY_EVENT_SINGLE_STEP  25
#define HVM_PARAM_BUFIOREQ_EVTCHN           26
#define HVM_PARAM_MEMORY_EVENT_MSR          30

#endif

#define HVM_PARAM_CALLBACK_IRQ 0
#define HVM_PARAM_CALLBACK_IRQ_TYPE_MASK xen_mk_ullong(0xFF00000000000000)

#define HVM_PARAM_CALLBACK_TYPE_GSI      0

#define HVM_PARAM_CALLBACK_TYPE_PCI_INTX 1

#if defined(__i386__) || defined(__x86_64__)
#define HVM_PARAM_CALLBACK_TYPE_VECTOR   2
#elif defined(__arm__) || defined(__aarch64__)
#define HVM_PARAM_CALLBACK_TYPE_PPI      2
#define HVM_PARAM_CALLBACK_TYPE_PPI_FLAG_MASK      0xFF00
#define HVM_PARAM_CALLBACK_TYPE_PPI_FLAG_LOW_LEVEL 2
#endif

#define HVM_PARAM_STORE_PFN    1
#define HVM_PARAM_STORE_EVTCHN 2

#define HVM_PARAM_IOREQ_PFN    5

#define HVM_PARAM_BUFIOREQ_PFN 6

#if defined(__i386__) || defined(__x86_64__)

#define HVM_PARAM_VIRIDIAN     9

#define _HVMPV_base_freq 0
#define HVMPV_base_freq  (1 << _HVMPV_base_freq)

#define _HVMPV_no_freq 1
#define HVMPV_no_freq  (1 << _HVMPV_no_freq)

#define _HVMPV_time_ref_count 2
#define HVMPV_time_ref_count  (1 << _HVMPV_time_ref_count)

#define _HVMPV_reference_tsc 3
#define HVMPV_reference_tsc  (1 << _HVMPV_reference_tsc)

#define _HVMPV_hcall_remote_tlb_flush 4
#define HVMPV_hcall_remote_tlb_flush (1 << _HVMPV_hcall_remote_tlb_flush)

#define _HVMPV_apic_assist 5
#define HVMPV_apic_assist (1 << _HVMPV_apic_assist)

#define _HVMPV_crash_ctl 6
#define HVMPV_crash_ctl (1 << _HVMPV_crash_ctl)

#define _HVMPV_synic 7
#define HVMPV_synic (1 << _HVMPV_synic)

#define _HVMPV_stimer 8
#define HVMPV_stimer (1 << _HVMPV_stimer)

#define _HVMPV_hcall_ipi 9
#define HVMPV_hcall_ipi (1 << _HVMPV_hcall_ipi)

#define _HVMPV_ex_processor_masks 10
#define HVMPV_ex_processor_masks (1 << _HVMPV_ex_processor_masks)

#define _HVMPV_no_vp_limit 11
#define HVMPV_no_vp_limit (1 << _HVMPV_no_vp_limit)

#define _HVMPV_cpu_hotplug 12
#define HVMPV_cpu_hotplug (1 << _HVMPV_cpu_hotplug)

#define HVMPV_feature_mask \
        (HVMPV_base_freq | \
         HVMPV_no_freq | \
         HVMPV_time_ref_count | \
         HVMPV_reference_tsc | \
         HVMPV_hcall_remote_tlb_flush | \
         HVMPV_apic_assist | \
         HVMPV_crash_ctl | \
         HVMPV_synic | \
         HVMPV_stimer | \
         HVMPV_hcall_ipi | \
         HVMPV_ex_processor_masks | \
         HVMPV_no_vp_limit | \
         HVMPV_cpu_hotplug)

#endif

#define HVM_PARAM_TIMER_MODE   10
#define HVMPTM_delay_for_missed_ticks    0
#define HVMPTM_no_delay_for_missed_ticks 1
#define HVMPTM_no_missed_ticks_pending   2
#define HVMPTM_one_missed_tick_pending   3

#define HVM_PARAM_HPET_ENABLED 11

#define HVM_PARAM_IDENT_PT     12

#define HVM_PARAM_ACPI_S_STATE 14

#define HVM_PARAM_VM86_TSS     15

#define HVM_PARAM_VPT_ALIGN    16

#define HVM_PARAM_CONSOLE_PFN    17
#define HVM_PARAM_CONSOLE_EVTCHN 18

#define HVM_PARAM_ACPI_IOPORTS_LOCATION 19

#define HVM_PARAM_PAGING_RING_PFN   27
#define HVM_PARAM_MONITOR_RING_PFN  28
#define HVM_PARAM_SHARING_RING_PFN  29

#define HVM_PARAM_TRIPLE_FAULT_REASON 31

#define HVM_PARAM_IOREQ_SERVER_PFN 32
#define HVM_PARAM_NR_IOREQ_SERVER_PAGES 33

#define HVM_PARAM_VM_GENERATION_ID_ADDR 34

#define HVM_PARAM_ALTP2M       35
#define XEN_ALTP2M_disabled      0
#define XEN_ALTP2M_mixed         1
#define XEN_ALTP2M_external      2
#define XEN_ALTP2M_limited       3

#define HVM_PARAM_X87_FIP_WIDTH 36

#define HVM_PARAM_VM86_TSS_SIZED 37

#define HVM_PARAM_MCA_CAP 38
#define XEN_HVM_MCA_CAP_LMCE   (xen_mk_ullong(1) << 0)
#define XEN_HVM_MCA_CAP_MASK   XEN_HVM_MCA_CAP_LMCE

#define HVM_NR_PARAMS 39

#endif
