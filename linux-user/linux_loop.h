/* Copied from 2.6.25 kernel headers to avoid problems on older hosts,
 * and subsequently updated to match newer additions to the API.
 */

#ifndef LINUX_LOOP_H
#define LINUX_LOOP_H

/*
 * include/linux/loop.h
 *
 * Written by Theodore Ts'o, 3/29/93.
 *
 * Copyright 1993 by Theodore Ts'o.  Redistribution of this file is
 * permitted under the GNU General Public License.
 */

#define LO_NAME_SIZE	64
#define LO_KEY_SIZE	32


enum {
	LO_FLAGS_READ_ONLY	= 1,
	LO_FLAGS_USE_AOPS	= 2,
	LO_FLAGS_AUTOCLEAR	= 4,
};

#include <linux/version.h>
#include <asm/posix_types.h>
#include <asm/types.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define __kernel_old_dev_t __kernel_dev_t
#endif

struct loop_info {
	int		   lo_number;
	__kernel_old_dev_t lo_device;
	unsigned long	   lo_inode;
	__kernel_old_dev_t lo_rdevice;
	int		   lo_offset;
	int		   lo_encrypt_type;
	int		   lo_encrypt_key_size;
	int		   lo_flags;
	char		   lo_name[LO_NAME_SIZE];
	unsigned char	   lo_encrypt_key[LO_KEY_SIZE];
	unsigned long	   lo_init[2];
	char		   reserved[4];
};

struct loop_info64 {
	__u64		   lo_device;
	__u64		   lo_inode;
	__u64		   lo_rdevice;
	__u64		   lo_offset;
	__u64		   lo_sizelimit;
	__u32		   lo_number;
	__u32		   lo_encrypt_type;
	__u32		   lo_encrypt_key_size;
	__u32		   lo_flags;
	__u8		   lo_file_name[LO_NAME_SIZE];
	__u8		   lo_crypt_name[LO_NAME_SIZE];
	__u8		   lo_encrypt_key[LO_KEY_SIZE];
	__u64		   lo_init[2];
};

#define LO_CRYPT_NONE		0
#define LO_CRYPT_XOR		1
#define LO_CRYPT_DES		2
#define LO_CRYPT_FISH2		3
#define LO_CRYPT_BLOW		4
#define LO_CRYPT_CAST128	5
#define LO_CRYPT_IDEA		6
#define LO_CRYPT_DUMMY		9
#define LO_CRYPT_SKIPJACK	10
#define LO_CRYPT_CRYPTOAPI	18
#define MAX_LO_CRYPT		20

#define LOOP_SET_FD		0x4C00
#define LOOP_CLR_FD		0x4C01
#define LOOP_SET_STATUS		0x4C02
#define LOOP_GET_STATUS		0x4C03
#define LOOP_SET_STATUS64	0x4C04
#define LOOP_GET_STATUS64	0x4C05
#define LOOP_CHANGE_FD		0x4C06
#define LOOP_SET_CAPACITY       0x4C07
#define LOOP_SET_DIRECT_IO      0x4C08
#define LOOP_SET_BLOCK_SIZE     0x4C09
#define LOOP_CONFIGURE          0x4C0A

#define LOOP_CTL_ADD            0x4C80
#define LOOP_CTL_REMOVE         0x4C81
#define LOOP_CTL_GET_FREE       0x4C82

#endif
