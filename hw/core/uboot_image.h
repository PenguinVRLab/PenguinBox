/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * (C) Copyright 2008 Semihalf
 *
 * (C) Copyright 2000-2005
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 ********************************************************************
 * NOTE: This header file defines an interface to U-Boot. Including
 * this (unmodified) header file in another file is considered normal
 * use of U-Boot, and does *not* fall under the heading of "derived
 * work".
 ********************************************************************
 */

#ifndef UBOOT_IMAGE_H
#define UBOOT_IMAGE_H

enum {
	IH_OS_INVALID		= 0,
	IH_OS_OPENBSD,
	IH_OS_NETBSD,
	IH_OS_FREEBSD,
	IH_OS_4_4BSD,
	IH_OS_LINUX,
	IH_OS_SVR4,
	IH_OS_ESIX,
	IH_OS_SOLARIS,
	IH_OS_IRIX,
	IH_OS_SCO,
	IH_OS_DELL,
	IH_OS_NCR,
	IH_OS_LYNXOS,
	IH_OS_VXWORKS,
	IH_OS_PSOS,
	IH_OS_QNX,
	IH_OS_U_BOOT,
	IH_OS_RTEMS,
	IH_OS_ARTOS,
	IH_OS_UNITY,
	IH_OS_INTEGRITY,
	IH_OS_OSE,
	IH_OS_PLAN9,
	IH_OS_OPENRTOS,
	IH_OS_ARM_TRUSTED_FIRMWARE,
	IH_OS_TEE,
	IH_OS_OPENSBI,
	IH_OS_EFI,

	IH_OS_COUNT,
};

enum {
	IH_ARCH_INVALID		= 0,
	IH_ARCH_ALPHA,
	IH_ARCH_ARM,
	IH_ARCH_I386,
	IH_ARCH_IA64,
	IH_ARCH_MIPS,
	IH_ARCH_MIPS64,
	IH_ARCH_PPC,
	IH_ARCH_S390,
	IH_ARCH_SH,
	IH_ARCH_SPARC,
	IH_ARCH_SPARC64,
	IH_ARCH_M68K,
	IH_ARCH_NIOS,
	IH_ARCH_MICROBLAZE,
	IH_ARCH_NIOS2,
	IH_ARCH_BLACKFIN,
	IH_ARCH_AVR32,
	IH_ARCH_ST200,
	IH_ARCH_SANDBOX,
	IH_ARCH_NDS32,
	IH_ARCH_OPENRISC,
	IH_ARCH_ARM64,
	IH_ARCH_ARC,
	IH_ARCH_X86_64,
	IH_ARCH_XTENSA,
	IH_ARCH_RISCV,

	IH_ARCH_COUNT,
};

enum {
	IH_TYPE_INVALID		= 0,
	IH_TYPE_STANDALONE,
	IH_TYPE_KERNEL,
	IH_TYPE_RAMDISK,
	IH_TYPE_MULTI,
	IH_TYPE_FIRMWARE,
	IH_TYPE_SCRIPT,
	IH_TYPE_FILESYSTEM,
	IH_TYPE_FLATDT,
	IH_TYPE_KWBIMAGE,
	IH_TYPE_IMXIMAGE,
	IH_TYPE_UBLIMAGE,
	IH_TYPE_OMAPIMAGE,
	IH_TYPE_AISIMAGE,
	IH_TYPE_KERNEL_NOLOAD,
	IH_TYPE_PBLIMAGE,
	IH_TYPE_MXSIMAGE,
	IH_TYPE_GPIMAGE,
	IH_TYPE_ATMELIMAGE,
	IH_TYPE_SOCFPGAIMAGE,
	IH_TYPE_X86_SETUP,
	IH_TYPE_LPC32XXIMAGE,
	IH_TYPE_LOADABLE,
	IH_TYPE_RKIMAGE,
	IH_TYPE_RKSD,
	IH_TYPE_RKSPI,
	IH_TYPE_ZYNQIMAGE,
	IH_TYPE_ZYNQMPIMAGE,
	IH_TYPE_ZYNQMPBIF,
	IH_TYPE_FPGA,
	IH_TYPE_VYBRIDIMAGE,
	IH_TYPE_TEE,
	IH_TYPE_FIRMWARE_IVT,
	IH_TYPE_PMMC,
	IH_TYPE_STM32IMAGE,
	IH_TYPE_SOCFPGAIMAGE_V1,
	IH_TYPE_MTKIMAGE,
	IH_TYPE_IMX8MIMAGE,
	IH_TYPE_IMX8IMAGE,
	IH_TYPE_COPRO,
	IH_TYPE_SUNXI_EGON,

	IH_TYPE_COUNT,
};

enum {
	IH_COMP_NONE		= 0,
	IH_COMP_GZIP,
	IH_COMP_BZIP2,
	IH_COMP_LZMA,
	IH_COMP_LZO,
	IH_COMP_LZ4,
	IH_COMP_ZSTD,

	IH_COMP_COUNT,
};

#define IH_MAGIC	0x27051956
#define IH_NMLEN		32

typedef struct uboot_image_header {
	uint32_t	ih_magic;
	uint32_t	ih_hcrc;
	uint32_t	ih_time;
	uint32_t	ih_size;
	uint32_t	ih_load;
	uint32_t	ih_ep;
	uint32_t	ih_dcrc;
	uint8_t		ih_os;
	uint8_t		ih_arch;
	uint8_t		ih_type;
	uint8_t		ih_comp;
	uint8_t		ih_name[IH_NMLEN];
} uboot_image_header_t;


#endif
