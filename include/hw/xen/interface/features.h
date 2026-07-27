/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * features.h
 *
 * Feature flags, reported by XENVER_get_features.
 *
 * Copyright (c) 2006, Keir Fraser <keir@xensource.com>
 */

#ifndef __XEN_PUBLIC_FEATURES_H__
#define __XEN_PUBLIC_FEATURES_H__

#define XENFEAT_writable_page_tables       0

#define XENFEAT_writable_descriptor_tables 1

#define XENFEAT_auto_translated_physmap    2

#define XENFEAT_supervisor_mode_kernel     3

#define XENFEAT_pae_pgdir_above_4gb        4

#define XENFEAT_mmu_pt_update_preserve_ad  5

#define XENFEAT_highmem_assist             6

#define XENFEAT_gnttab_map_avail_bits      7

#define XENFEAT_hvm_callback_vector        8

#define XENFEAT_hvm_safe_pvclock           9

#define XENFEAT_hvm_pirqs                 10

#define XENFEAT_dom0                      11

#define XENFEAT_memory_op_vnode_supported 13

#define XENFEAT_ARM_SMCCC_supported       14

#define XENFEAT_linux_rsdp_unrestricted   15

#define XENFEAT_not_direct_mapped         16
#define XENFEAT_direct_mapped             17

#define XENFEAT_NR_SUBMAPS 1

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
