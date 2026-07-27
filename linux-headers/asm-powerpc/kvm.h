/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright IBM Corp. 2007
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __LINUX_KVM_POWERPC_H
#define __LINUX_KVM_POWERPC_H

#include <linux/types.h>

#define __KVM_HAVE_SPAPR_TCE
#define __KVM_HAVE_PPC_SMT
#define __KVM_HAVE_IRQCHIP
#define __KVM_HAVE_IRQ_LINE

#define KVM_COALESCED_MMIO_PAGE_OFFSET 1

struct kvm_regs {
	__u64 pc;
	__u64 cr;
	__u64 ctr;
	__u64 lr;
	__u64 xer;
	__u64 msr;
	__u64 srr0;
	__u64 srr1;
	__u64 pid;

	__u64 sprg0;
	__u64 sprg1;
	__u64 sprg2;
	__u64 sprg3;
	__u64 sprg4;
	__u64 sprg5;
	__u64 sprg6;
	__u64 sprg7;

	__u64 gpr[32];
};

#define KVM_SREGS_E_IMPL_NONE	0
#define KVM_SREGS_E_IMPL_FSL	1

#define KVM_SREGS_E_FSL_PIDn	(1 << 0)

#define KVM_RUN_PPC_NMI_DISP_MASK		(3 << 0)
#define   KVM_RUN_PPC_NMI_DISP_FULLY_RECOV	(1 << 0)
#define   KVM_RUN_PPC_NMI_DISP_LIMITED_RECOV	(2 << 0)
#define   KVM_RUN_PPC_NMI_DISP_NOT_RECOV	(3 << 0)

#define KVM_SREGS_E_BASE		(1 << 0)

#define KVM_SREGS_E_ARCH206		(1 << 1)

#define KVM_SREGS_E_64			(1 << 2)

#define KVM_SREGS_E_SPRG8		(1 << 3)
#define KVM_SREGS_E_MCIVPR		(1 << 4)

#define KVM_SREGS_E_IVOR		(1 << 5)

#define KVM_SREGS_E_ARCH206_MMU		(1 << 6)

#define KVM_SREGS_E_DEBUG		(1 << 7)

#define KVM_SREGS_E_ED			(1 << 8)

#define KVM_SREGS_E_SPE			(1 << 9)

#define KVM_SREGS_EXP			(1 << 10)

#define KVM_SREGS_E_PD			(1 << 11)

#define KVM_SREGS_E_PC			(1 << 12)

#define KVM_SREGS_E_PT			(1 << 13)

#define KVM_SREGS_E_PM			(1 << 14)

#define KVM_SREGS_E_UPDATE_MCSR		(1 << 0)
#define KVM_SREGS_E_UPDATE_TSR		(1 << 1)
#define KVM_SREGS_E_UPDATE_DEC		(1 << 2)
#define KVM_SREGS_E_UPDATE_DBSR		(1 << 3)

struct kvm_sregs {
	__u32 pvr;
	union {
		struct {
			__u64 sdr1;
			struct {
				struct {
					__u64 slbe;
					__u64 slbv;
				} slb[64];
			} ppc64;
			struct {
				__u32 sr[16];
				__u64 ibat[8];
				__u64 dbat[8];
			} ppc32;
		} s;
		struct {
			union {
				struct {
					__u32 features;
					__u32 svr;
					__u64 mcar;
					__u32 hid0;

					__u32 pid1, pid2;
				} fsl;
				__u8 pad[256];
			} impl;

			__u32 features;
			__u32 impl_id;
			__u32 update_special;
			__u32 pir;
			__u64 sprg8;
			__u64 sprg9;
			__u64 csrr0;
			__u64 dsrr0;
			__u64 mcsrr0;
			__u32 csrr1;
			__u32 dsrr1;
			__u32 mcsrr1;
			__u32 esr;
			__u64 dear;
			__u64 ivpr;
			__u64 mcivpr;
			__u64 mcsr;

			__u32 tsr;
			__u32 tcr;
			__u32 decar;
			__u32 dec;

			__u64 tb;

			__u32 dbsr;
			__u32 dbcr[3];
			__u32 iac[4];
			__u32 dac[2];
			__u32 dvc[2];
			__u8 num_iac;
			__u8 num_dac;
			__u8 num_dvc;
			__u8 pad;

			__u32 epr;
			__u32 vrsave;
			__u32 epcr;

			__u32 mas0;
			__u32 mas1;
			__u64 mas2;
			__u64 mas7_3;
			__u32 mas4;
			__u32 mas6;

			__u32 ivor_low[16];
			__u32 ivor_high[18];

			__u32 mmucfg;
			__u32 eptcfg;
			__u32 tlbcfg[4];
			__u32 tlbps[4];

			__u32 eplc, epsc;
		} e;
		__u8 pad[1020];
	} u;
};

struct kvm_fpu {
	__u64 fpr[32];
};

#define KVMPPC_DEBUG_NONE		0x0
#define KVMPPC_DEBUG_BREAKPOINT		(1UL << 1)
#define KVMPPC_DEBUG_WATCH_WRITE	(1UL << 2)
#define KVMPPC_DEBUG_WATCH_READ		(1UL << 3)
struct kvm_debug_exit_arch {
	__u64 address;
	__u32 status;
	__u32 reserved;
};

struct kvm_guest_debug_arch {
	struct {
		__u64 addr;
		__u32 type;
		__u32 reserved;
	} bp[16];
};

#define KVM_GUESTDBG_USE_SW_BP		0x00010000
#define KVM_GUESTDBG_USE_HW_BP		0x00020000

struct kvm_sync_regs {
};

#define KVM_INTERRUPT_SET	-1U
#define KVM_INTERRUPT_UNSET	-2U
#define KVM_INTERRUPT_SET_LEVEL	-3U

#define KVM_CPU_440		1
#define KVM_CPU_E500V2		2
#define KVM_CPU_3S_32		3
#define KVM_CPU_3S_64		4
#define KVM_CPU_E500MC		5

struct kvm_create_spapr_tce {
	__u64 liobn;
	__u32 window_size;
};

struct kvm_create_spapr_tce_64 {
	__u64 liobn;
	__u32 page_shift;
	__u32 flags;
	__u64 offset;
	__u64 size;
};

struct kvm_allocate_rma {
	__u64 rma_size;
};

struct kvm_rtas_token_args {
	char name[120];
	__u64 token;
};

struct kvm_book3e_206_tlb_entry {
	__u32 mas8;
	__u32 mas1;
	__u64 mas2;
	__u64 mas7_3;
};

struct kvm_book3e_206_tlb_params {
	__u32 tlb_sizes[4];
	__u32 tlb_ways[4];
	__u32 reserved[8];
};

struct kvm_get_htab_fd {
	__u64	flags;
	__u64	start_index;
	__u64	reserved[2];
};

#define KVM_GET_HTAB_BOLTED_ONLY	((__u64)0x1)
#define KVM_GET_HTAB_WRITE		((__u64)0x2)

struct kvm_get_htab_header {
	__u32	index;
	__u16	n_valid;
	__u16	n_invalid;
};

struct kvm_ppc_mmuv3_cfg {
	__u64	flags;
	__u64	process_table;
};

#define KVM_PPC_MMUV3_RADIX	1
#define KVM_PPC_MMUV3_GTSE	2

struct kvm_ppc_rmmu_info {
	struct kvm_ppc_radix_geom {
		__u8	page_shift;
		__u8	level_bits[4];
		__u8	pad[3];
	}	geometries[8];
	__u32	ap_encodings[8];
};

struct kvm_ppc_cpu_char {
	__u64	character;
	__u64	behaviour;
	__u64	character_mask;
	__u64	behaviour_mask;
};

#define KVM_PPC_CPU_CHAR_SPEC_BAR_ORI31		(1ULL << 63)
#define KVM_PPC_CPU_CHAR_BCCTRL_SERIALISED	(1ULL << 62)
#define KVM_PPC_CPU_CHAR_L1D_FLUSH_ORI30	(1ULL << 61)
#define KVM_PPC_CPU_CHAR_L1D_FLUSH_TRIG2	(1ULL << 60)
#define KVM_PPC_CPU_CHAR_L1D_THREAD_PRIV	(1ULL << 59)
#define KVM_PPC_CPU_CHAR_BR_HINT_HONOURED	(1ULL << 58)
#define KVM_PPC_CPU_CHAR_MTTRIG_THR_RECONF	(1ULL << 57)
#define KVM_PPC_CPU_CHAR_COUNT_CACHE_DIS	(1ULL << 56)
#define KVM_PPC_CPU_CHAR_BCCTR_FLUSH_ASSIST	(1ull << 54)

#define KVM_PPC_CPU_BEHAV_FAVOUR_SECURITY	(1ULL << 63)
#define KVM_PPC_CPU_BEHAV_L1D_FLUSH_PR		(1ULL << 62)
#define KVM_PPC_CPU_BEHAV_BNDS_CHK_SPEC_BAR	(1ULL << 61)
#define KVM_PPC_CPU_BEHAV_FLUSH_COUNT_CACHE	(1ull << 58)

#define KVM_REG_PPC_ICP_STATE	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x8c)

#define  KVM_REG_PPC_ICP_CPPR_SHIFT	56
#define  KVM_REG_PPC_ICP_CPPR_MASK	0xff
#define  KVM_REG_PPC_ICP_XISR_SHIFT	32
#define  KVM_REG_PPC_ICP_XISR_MASK	0xffffff
#define  KVM_REG_PPC_ICP_MFRR_SHIFT	24
#define  KVM_REG_PPC_ICP_MFRR_MASK	0xff
#define  KVM_REG_PPC_ICP_PPRI_SHIFT	16
#define  KVM_REG_PPC_ICP_PPRI_MASK	0xff

#define KVM_REG_PPC_VP_STATE	(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x8d)

#define KVM_DEV_MPIC_GRP_MISC		1
#define   KVM_DEV_MPIC_BASE_ADDR	0

#define KVM_DEV_MPIC_GRP_REGISTER	2
#define KVM_DEV_MPIC_GRP_IRQ_ACTIVE	3

#define KVM_REG_PPC_HIOR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x1)
#define KVM_REG_PPC_IAC1	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x2)
#define KVM_REG_PPC_IAC2	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x3)
#define KVM_REG_PPC_IAC3	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x4)
#define KVM_REG_PPC_IAC4	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x5)
#define KVM_REG_PPC_DAC1	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x6)
#define KVM_REG_PPC_DAC2	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x7)
#define KVM_REG_PPC_DABR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x8)
#define KVM_REG_PPC_DSCR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x9)
#define KVM_REG_PPC_PURR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xa)
#define KVM_REG_PPC_SPURR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xb)
#define KVM_REG_PPC_DAR		(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xc)
#define KVM_REG_PPC_DSISR	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0xd)
#define KVM_REG_PPC_AMR		(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xe)
#define KVM_REG_PPC_UAMOR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xf)

#define KVM_REG_PPC_MMCR0	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x10)
#define KVM_REG_PPC_MMCR1	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x11)
#define KVM_REG_PPC_MMCRA	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x12)
#define KVM_REG_PPC_MMCR2	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x13)
#define KVM_REG_PPC_MMCRS	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x14)
#define KVM_REG_PPC_SIAR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x15)
#define KVM_REG_PPC_SDAR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x16)
#define KVM_REG_PPC_SIER	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x17)

#define KVM_REG_PPC_PMC1	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x18)
#define KVM_REG_PPC_PMC2	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x19)
#define KVM_REG_PPC_PMC3	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x1a)
#define KVM_REG_PPC_PMC4	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x1b)
#define KVM_REG_PPC_PMC5	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x1c)
#define KVM_REG_PPC_PMC6	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x1d)
#define KVM_REG_PPC_PMC7	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x1e)
#define KVM_REG_PPC_PMC8	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x1f)

#define KVM_REG_PPC_FPR0	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x20)
#define KVM_REG_PPC_FPR(n)	(KVM_REG_PPC_FPR0 + (n))
#define KVM_REG_PPC_FPR31	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x3f)

#define KVM_REG_PPC_VR0		(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x40)
#define KVM_REG_PPC_VR(n)	(KVM_REG_PPC_VR0 + (n))
#define KVM_REG_PPC_VR31	(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x5f)

#define KVM_REG_PPC_VSR0	(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x60)
#define KVM_REG_PPC_VSR(n)	(KVM_REG_PPC_VSR0 + (n))
#define KVM_REG_PPC_VSR31	(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x7f)

#define KVM_REG_PPC_FPSCR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x80)
#define KVM_REG_PPC_VSCR	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x81)

#define KVM_REG_PPC_VPA_ADDR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x82)
#define KVM_REG_PPC_VPA_SLB	(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x83)
#define KVM_REG_PPC_VPA_DTL	(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x84)

#define KVM_REG_PPC_EPCR	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x85)
#define KVM_REG_PPC_EPR		(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x86)

#define KVM_REG_PPC_OR_TSR	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x87)
#define KVM_REG_PPC_CLEAR_TSR	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x88)
#define KVM_REG_PPC_TCR		(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x89)
#define KVM_REG_PPC_TSR		(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x8a)

#define KVM_REG_PPC_DEBUG_INST	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x8b)

#define KVM_REG_PPC_MAS0	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x8c)
#define KVM_REG_PPC_MAS1	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x8d)
#define KVM_REG_PPC_MAS2	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x8e)
#define KVM_REG_PPC_MAS7_3	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x8f)
#define KVM_REG_PPC_MAS4	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x90)
#define KVM_REG_PPC_MAS6	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x91)
#define KVM_REG_PPC_MMUCFG	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x92)
#define KVM_REG_PPC_TLB0CFG	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x93)
#define KVM_REG_PPC_TLB1CFG	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x94)
#define KVM_REG_PPC_TLB2CFG	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x95)
#define KVM_REG_PPC_TLB3CFG	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x96)
#define KVM_REG_PPC_TLB0PS	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x97)
#define KVM_REG_PPC_TLB1PS	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x98)
#define KVM_REG_PPC_TLB2PS	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x99)
#define KVM_REG_PPC_TLB3PS	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x9a)
#define KVM_REG_PPC_EPTCFG	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x9b)

#define KVM_REG_PPC_TB_OFFSET	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x9c)

#define KVM_REG_PPC_SPMC1	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x9d)
#define KVM_REG_PPC_SPMC2	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x9e)
#define KVM_REG_PPC_IAMR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x9f)
#define KVM_REG_PPC_TFHAR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xa0)
#define KVM_REG_PPC_TFIAR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xa1)
#define KVM_REG_PPC_TEXASR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xa2)
#define KVM_REG_PPC_FSCR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xa3)
#define KVM_REG_PPC_PSPB	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0xa4)
#define KVM_REG_PPC_EBBHR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xa5)
#define KVM_REG_PPC_EBBRR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xa6)
#define KVM_REG_PPC_BESCR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xa7)
#define KVM_REG_PPC_TAR		(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xa8)
#define KVM_REG_PPC_DPDES	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xa9)
#define KVM_REG_PPC_DAWR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xaa)
#define KVM_REG_PPC_DAWRX	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xab)
#define KVM_REG_PPC_CIABR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xac)
#define KVM_REG_PPC_IC		(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xad)
#define KVM_REG_PPC_VTB		(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xae)
#define KVM_REG_PPC_CSIGR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xaf)
#define KVM_REG_PPC_TACR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xb0)
#define KVM_REG_PPC_TCSCR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xb1)
#define KVM_REG_PPC_PID		(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xb2)
#define KVM_REG_PPC_ACOP	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xb3)

#define KVM_REG_PPC_VRSAVE	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0xb4)
#define KVM_REG_PPC_LPCR	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0xb5)
#define KVM_REG_PPC_LPCR_64	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xb5)
#define KVM_REG_PPC_PPR		(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xb6)

#define KVM_REG_PPC_ARCH_COMPAT	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0xb7)

#define KVM_REG_PPC_DABRX	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0xb8)
#define KVM_REG_PPC_WORT	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xb9)
#define KVM_REG_PPC_SPRG9	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xba)
#define KVM_REG_PPC_DBSR	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0xbb)

#define KVM_REG_PPC_TIDR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xbc)
#define KVM_REG_PPC_PSSCR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xbd)

#define KVM_REG_PPC_DEC_EXPIRY	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xbe)
#define KVM_REG_PPC_ONLINE	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0xbf)
#define KVM_REG_PPC_PTCR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xc0)

#define KVM_REG_PPC_MMCR3	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xc1)
#define KVM_REG_PPC_SIER2	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xc2)
#define KVM_REG_PPC_SIER3	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xc3)
#define KVM_REG_PPC_DAWR1	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xc4)
#define KVM_REG_PPC_DAWRX1	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xc5)
#define KVM_REG_PPC_DEXCR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xc6)
#define KVM_REG_PPC_HASHKEYR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xc7)
#define KVM_REG_PPC_HASHPKEYR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xc8)

#define KVM_REG_PPC_TM		(KVM_REG_PPC | 0x80000000)
#define KVM_REG_PPC_TM_GPR0	(KVM_REG_PPC_TM | KVM_REG_SIZE_U64 | 0)
#define KVM_REG_PPC_TM_GPR(n)	(KVM_REG_PPC_TM_GPR0 + (n))
#define KVM_REG_PPC_TM_GPR31	(KVM_REG_PPC_TM | KVM_REG_SIZE_U64 | 0x1f)
#define KVM_REG_PPC_TM_VSR0	(KVM_REG_PPC_TM | KVM_REG_SIZE_U128 | 0x20)
#define KVM_REG_PPC_TM_VSR(n)	(KVM_REG_PPC_TM_VSR0 + (n))
#define KVM_REG_PPC_TM_VSR63	(KVM_REG_PPC_TM | KVM_REG_SIZE_U128 | 0x5f)
#define KVM_REG_PPC_TM_CR	(KVM_REG_PPC_TM | KVM_REG_SIZE_U64 | 0x60)
#define KVM_REG_PPC_TM_LR	(KVM_REG_PPC_TM | KVM_REG_SIZE_U64 | 0x61)
#define KVM_REG_PPC_TM_CTR	(KVM_REG_PPC_TM | KVM_REG_SIZE_U64 | 0x62)
#define KVM_REG_PPC_TM_FPSCR	(KVM_REG_PPC_TM | KVM_REG_SIZE_U64 | 0x63)
#define KVM_REG_PPC_TM_AMR	(KVM_REG_PPC_TM | KVM_REG_SIZE_U64 | 0x64)
#define KVM_REG_PPC_TM_PPR	(KVM_REG_PPC_TM | KVM_REG_SIZE_U64 | 0x65)
#define KVM_REG_PPC_TM_VRSAVE	(KVM_REG_PPC_TM | KVM_REG_SIZE_U64 | 0x66)
#define KVM_REG_PPC_TM_VSCR	(KVM_REG_PPC_TM | KVM_REG_SIZE_U32 | 0x67)
#define KVM_REG_PPC_TM_DSCR	(KVM_REG_PPC_TM | KVM_REG_SIZE_U64 | 0x68)
#define KVM_REG_PPC_TM_TAR	(KVM_REG_PPC_TM | KVM_REG_SIZE_U64 | 0x69)
#define KVM_REG_PPC_TM_XER	(KVM_REG_PPC_TM | KVM_REG_SIZE_U64 | 0x6a)

#define KVM_DEV_XICS_GRP_SOURCES	1
#define KVM_DEV_XICS_GRP_CTRL		2
#define   KVM_DEV_XICS_NR_SERVERS	1

#define  KVM_XICS_DESTINATION_SHIFT	0
#define  KVM_XICS_DESTINATION_MASK	0xffffffffULL
#define  KVM_XICS_PRIORITY_SHIFT	32
#define  KVM_XICS_PRIORITY_MASK		0xff
#define  KVM_XICS_LEVEL_SENSITIVE	(1ULL << 40)
#define  KVM_XICS_MASKED		(1ULL << 41)
#define  KVM_XICS_PENDING		(1ULL << 42)
#define  KVM_XICS_PRESENTED		(1ULL << 43)
#define  KVM_XICS_QUEUED		(1ULL << 44)

#define KVM_DEV_XIVE_GRP_CTRL		1
#define   KVM_DEV_XIVE_RESET		1
#define   KVM_DEV_XIVE_EQ_SYNC		2
#define   KVM_DEV_XIVE_NR_SERVERS	3
#define KVM_DEV_XIVE_GRP_SOURCE		2
#define KVM_DEV_XIVE_GRP_SOURCE_CONFIG	3
#define KVM_DEV_XIVE_GRP_EQ_CONFIG	4
#define KVM_DEV_XIVE_GRP_SOURCE_SYNC	5

#define KVM_XIVE_LEVEL_SENSITIVE	(1ULL << 0)
#define KVM_XIVE_LEVEL_ASSERTED		(1ULL << 1)

#define KVM_XIVE_SOURCE_PRIORITY_SHIFT	0
#define KVM_XIVE_SOURCE_PRIORITY_MASK	0x7
#define KVM_XIVE_SOURCE_SERVER_SHIFT	3
#define KVM_XIVE_SOURCE_SERVER_MASK	0xfffffff8ULL
#define KVM_XIVE_SOURCE_MASKED_SHIFT	32
#define KVM_XIVE_SOURCE_MASKED_MASK	0x100000000ULL
#define KVM_XIVE_SOURCE_EISN_SHIFT	33
#define KVM_XIVE_SOURCE_EISN_MASK	0xfffffffe00000000ULL

#define KVM_XIVE_EQ_PRIORITY_SHIFT	0
#define KVM_XIVE_EQ_PRIORITY_MASK	0x7
#define KVM_XIVE_EQ_SERVER_SHIFT	3
#define KVM_XIVE_EQ_SERVER_MASK		0xfffffff8ULL

struct kvm_ppc_xive_eq {
	__u32 flags;
	__u32 qshift;
	__u64 qaddr;
	__u32 qtoggle;
	__u32 qindex;
	__u8  pad[40];
};

#define KVM_XIVE_EQ_ALWAYS_NOTIFY	0x00000001

#define KVM_XIVE_TIMA_PAGE_OFFSET	0
#define KVM_XIVE_ESB_PAGE_OFFSET	4

#define KVM_PPC_PVINFO_FLAGS_EV_IDLE   (1<<0)

struct kvm_ppc_pvinfo {
	__u32 flags;
	__u32 hcall[4];
	__u8  pad[108];
};

#define KVM_PPC_PAGE_SIZES_MAX_SZ	8

struct kvm_ppc_one_page_size {
	__u32 page_shift;
	__u32 pte_enc;
};

struct kvm_ppc_one_seg_page_size {
	__u32 page_shift;
	__u32 slb_enc;
	struct kvm_ppc_one_page_size enc[KVM_PPC_PAGE_SIZES_MAX_SZ];
};

#define KVM_PPC_PAGE_SIZES_REAL		0x00000001
#define KVM_PPC_1T_SEGMENTS		0x00000002
#define KVM_PPC_NO_HASH			0x00000004

struct kvm_ppc_smmu_info {
	__u64 flags;
	__u32 slb_size;
	__u16 data_keys;
	__u16 instr_keys;
	struct kvm_ppc_one_seg_page_size sps[KVM_PPC_PAGE_SIZES_MAX_SZ];
};

struct kvm_ppc_resize_hpt {
	__u64 flags;
	__u32 shift;
	__u32 pad;
};

#endif
