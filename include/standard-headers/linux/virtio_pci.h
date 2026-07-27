/*
 * Virtio PCI driver
 *
 * This module allows virtio devices to be used over a virtual PCI device.
 * This can be used with QEMU based VMMs like KVM or Xen.
 *
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _LINUX_VIRTIO_PCI_H
#define _LINUX_VIRTIO_PCI_H

#include "standard-headers/linux/types.h"
#include "standard-headers/linux/kernel.h"

#ifndef VIRTIO_PCI_NO_LEGACY

#define VIRTIO_PCI_HOST_FEATURES	0

#define VIRTIO_PCI_GUEST_FEATURES	4

#define VIRTIO_PCI_QUEUE_PFN		8

#define VIRTIO_PCI_QUEUE_NUM		12

#define VIRTIO_PCI_QUEUE_SEL		14

#define VIRTIO_PCI_QUEUE_NOTIFY		16

#define VIRTIO_PCI_STATUS		18

#define VIRTIO_PCI_ISR			19

#define VIRTIO_MSI_CONFIG_VECTOR        20
#define VIRTIO_MSI_QUEUE_VECTOR         22

#define VIRTIO_PCI_CONFIG_OFF(msix_enabled)	((msix_enabled) ? 24 : 20)
#define VIRTIO_PCI_CONFIG(dev)	VIRTIO_PCI_CONFIG_OFF((dev)->msix_enabled)

#define VIRTIO_PCI_ABI_VERSION		0

#define VIRTIO_PCI_QUEUE_ADDR_SHIFT	12

#define VIRTIO_PCI_VRING_ALIGN		4096

#endif

#define VIRTIO_PCI_ISR_CONFIG		0x2
#define VIRTIO_MSI_NO_VECTOR            0xffff

#ifndef VIRTIO_PCI_NO_MODERN

#define VIRTIO_PCI_CAP_COMMON_CFG	1
#define VIRTIO_PCI_CAP_NOTIFY_CFG	2
#define VIRTIO_PCI_CAP_ISR_CFG		3
#define VIRTIO_PCI_CAP_DEVICE_CFG	4
#define VIRTIO_PCI_CAP_PCI_CFG		5
#define VIRTIO_PCI_CAP_SHARED_MEMORY_CFG 8
#define VIRTIO_PCI_CAP_VENDOR_CFG	9

struct virtio_pci_cap {
	uint8_t cap_vndr;
	uint8_t cap_next;
	uint8_t cap_len;
	uint8_t cfg_type;
	uint8_t bar;
	uint8_t id;
	uint8_t padding[2];
	uint32_t offset;
	uint32_t length;
};

struct virtio_pci_vndr_data {
	uint8_t cap_vndr;
	uint8_t cap_next;
	uint8_t cap_len;
	uint8_t cfg_type;
	uint16_t vendor_id;
};

struct virtio_pci_cap64 {
	struct virtio_pci_cap cap;
	uint32_t offset_hi;
	uint32_t length_hi;
};

struct virtio_pci_notify_cap {
	struct virtio_pci_cap cap;
	uint32_t notify_off_multiplier;
};

struct virtio_pci_common_cfg {
	uint32_t device_feature_select;
	uint32_t device_feature;
	uint32_t guest_feature_select;
	uint32_t guest_feature;
	uint16_t msix_config;
	uint16_t num_queues;
	uint8_t device_status;
	uint8_t config_generation;

	uint16_t queue_select;
	uint16_t queue_size;
	uint16_t queue_msix_vector;
	uint16_t queue_enable;
	uint16_t queue_notify_off;
	uint32_t queue_desc_lo;
	uint32_t queue_desc_hi;
	uint32_t queue_avail_lo;
	uint32_t queue_avail_hi;
	uint32_t queue_used_lo;
	uint32_t queue_used_hi;
};

struct virtio_pci_modern_common_cfg {
	struct virtio_pci_common_cfg cfg;

	uint16_t queue_notify_data;
	uint16_t queue_reset;

	uint16_t admin_queue_index;
	uint16_t admin_queue_num;
};

struct virtio_pci_cfg_cap {
	struct virtio_pci_cap cap;
	uint8_t pci_cfg_data[4];
};

#define VIRTIO_PCI_CAP_VNDR		0
#define VIRTIO_PCI_CAP_NEXT		1
#define VIRTIO_PCI_CAP_LEN		2
#define VIRTIO_PCI_CAP_CFG_TYPE		3
#define VIRTIO_PCI_CAP_BAR		4
#define VIRTIO_PCI_CAP_OFFSET		8
#define VIRTIO_PCI_CAP_LENGTH		12

#define VIRTIO_PCI_NOTIFY_CAP_MULT	16

#define VIRTIO_PCI_COMMON_DFSELECT	0
#define VIRTIO_PCI_COMMON_DF		4
#define VIRTIO_PCI_COMMON_GFSELECT	8
#define VIRTIO_PCI_COMMON_GF		12
#define VIRTIO_PCI_COMMON_MSIX		16
#define VIRTIO_PCI_COMMON_NUMQ		18
#define VIRTIO_PCI_COMMON_STATUS	20
#define VIRTIO_PCI_COMMON_CFGGENERATION	21
#define VIRTIO_PCI_COMMON_Q_SELECT	22
#define VIRTIO_PCI_COMMON_Q_SIZE	24
#define VIRTIO_PCI_COMMON_Q_MSIX	26
#define VIRTIO_PCI_COMMON_Q_ENABLE	28
#define VIRTIO_PCI_COMMON_Q_NOFF	30
#define VIRTIO_PCI_COMMON_Q_DESCLO	32
#define VIRTIO_PCI_COMMON_Q_DESCHI	36
#define VIRTIO_PCI_COMMON_Q_AVAILLO	40
#define VIRTIO_PCI_COMMON_Q_AVAILHI	44
#define VIRTIO_PCI_COMMON_Q_USEDLO	48
#define VIRTIO_PCI_COMMON_Q_USEDHI	52
#define VIRTIO_PCI_COMMON_Q_NDATA	56
#define VIRTIO_PCI_COMMON_Q_RESET	58
#define VIRTIO_PCI_COMMON_ADM_Q_IDX	60
#define VIRTIO_PCI_COMMON_ADM_Q_NUM	62

#endif

#define VIRTIO_ADMIN_STATUS_OK		0

#define VIRTIO_ADMIN_CMD_LIST_QUERY	0x0
#define VIRTIO_ADMIN_CMD_LIST_USE	0x1

#define VIRTIO_ADMIN_GROUP_TYPE_SELF	0x0
#define VIRTIO_ADMIN_GROUP_TYPE_SRIOV	0x1

#define VIRTIO_ADMIN_CMD_LEGACY_COMMON_CFG_WRITE	0x2
#define VIRTIO_ADMIN_CMD_LEGACY_COMMON_CFG_READ		0x3
#define VIRTIO_ADMIN_CMD_LEGACY_DEV_CFG_WRITE		0x4
#define VIRTIO_ADMIN_CMD_LEGACY_DEV_CFG_READ		0x5
#define VIRTIO_ADMIN_CMD_LEGACY_NOTIFY_INFO		0x6

#define VIRTIO_ADMIN_CMD_CAP_ID_LIST_QUERY		0x7
#define VIRTIO_ADMIN_CMD_DEVICE_CAP_GET			0x8
#define VIRTIO_ADMIN_CMD_DRIVER_CAP_SET			0x9
#define VIRTIO_ADMIN_CMD_RESOURCE_OBJ_CREATE		0xa
#define VIRTIO_ADMIN_CMD_RESOURCE_OBJ_DESTROY		0xd
#define VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_GET		0xe
#define VIRTIO_ADMIN_CMD_DEV_PARTS_GET			0xf
#define VIRTIO_ADMIN_CMD_DEV_PARTS_SET			0x10
#define VIRTIO_ADMIN_CMD_DEV_MODE_SET			0x11

struct virtio_admin_cmd_hdr {
	uint16_t opcode;
	uint16_t group_type;
	uint8_t reserved1[12];
	uint64_t group_member_id;
};

struct virtio_admin_cmd_status {
	uint16_t status;
	uint16_t status_qualifier;
	uint8_t reserved2[4];
};

struct virtio_admin_cmd_legacy_wr_data {
	uint8_t offset;
	uint8_t reserved[7];
	uint8_t registers[];
};

struct virtio_admin_cmd_legacy_rd_data {
	uint8_t offset;
};

#define VIRTIO_ADMIN_CMD_NOTIFY_INFO_FLAGS_END 0
#define VIRTIO_ADMIN_CMD_NOTIFY_INFO_FLAGS_OWNER_DEV 0x1
#define VIRTIO_ADMIN_CMD_NOTIFY_INFO_FLAGS_OWNER_MEM 0x2

#define VIRTIO_ADMIN_CMD_MAX_NOTIFY_INFO 4

struct virtio_admin_cmd_notify_info_data {
	uint8_t flags;
	uint8_t bar;
	uint8_t padding[6];
	uint64_t offset;
};

struct virtio_admin_cmd_notify_info_result {
	struct virtio_admin_cmd_notify_info_data entries[VIRTIO_ADMIN_CMD_MAX_NOTIFY_INFO];
};

#define VIRTIO_DEV_PARTS_CAP 0x0000

struct virtio_dev_parts_cap {
	uint8_t get_parts_resource_objects_limit;
	uint8_t set_parts_resource_objects_limit;
};

#define MAX_CAP_ID __KERNEL_DIV_ROUND_UP(VIRTIO_DEV_PARTS_CAP + 1, 64)

struct virtio_admin_cmd_query_cap_id_result {
	uint64_t supported_caps[MAX_CAP_ID];
};

struct virtio_admin_cmd_cap_get_data {
	uint16_t id;
	uint8_t reserved[6];
};

struct virtio_admin_cmd_cap_set_data {
	uint16_t id;
	uint8_t reserved[6];
	uint8_t cap_specific_data[];
};

struct virtio_admin_cmd_resource_obj_cmd_hdr {
	uint16_t type;
	uint8_t reserved[2];
	uint32_t id;
};

struct virtio_admin_cmd_resource_obj_create_data {
	struct virtio_admin_cmd_resource_obj_cmd_hdr hdr;
	uint64_t flags;
	uint8_t resource_obj_specific_data[];
};

#define VIRTIO_RESOURCE_OBJ_DEV_PARTS 0

#define VIRTIO_RESOURCE_OBJ_DEV_PARTS_TYPE_GET 0
#define VIRTIO_RESOURCE_OBJ_DEV_PARTS_TYPE_SET 1

struct virtio_resource_obj_dev_parts {
	uint8_t type;
	uint8_t reserved[7];
};

#define VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_TYPE_SIZE 0
#define VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_TYPE_COUNT 1
#define VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_TYPE_LIST 2

struct virtio_admin_cmd_dev_parts_metadata_data {
	struct virtio_admin_cmd_resource_obj_cmd_hdr hdr;
	uint8_t type;
	uint8_t reserved[7];
};

#define VIRTIO_DEV_PART_F_OPTIONAL 0

struct virtio_dev_part_hdr {
	uint16_t part_type;
	uint8_t flags;
	uint8_t reserved;
	union {
		struct {
			uint32_t offset;
			uint32_t reserved;
		} pci_common_cfg;
		struct {
			uint16_t index;
			uint8_t reserved[6];
		} vq_index;
	} selector;
	uint32_t length;
};

struct virtio_dev_part {
	struct virtio_dev_part_hdr hdr;
	uint8_t value[];
};

struct virtio_admin_cmd_dev_parts_metadata_result {
	union {
		struct {
			uint32_t size;
			uint32_t reserved;
		} parts_size;
		struct {
			uint32_t count;
			uint32_t reserved;
		} hdr_list_count;
		struct {
			uint32_t count;
			uint32_t reserved;
			struct virtio_dev_part_hdr hdrs[];
		} hdr_list;
	};
};

#define VIRTIO_ADMIN_CMD_DEV_PARTS_GET_TYPE_SELECTED 0
#define VIRTIO_ADMIN_CMD_DEV_PARTS_GET_TYPE_ALL 1

struct virtio_admin_cmd_dev_parts_get_data {
	struct virtio_admin_cmd_resource_obj_cmd_hdr hdr;
	uint8_t type;
	uint8_t reserved[7];
	struct virtio_dev_part_hdr hdr_list[];
};

struct virtio_admin_cmd_dev_parts_set_data {
	struct virtio_admin_cmd_resource_obj_cmd_hdr hdr;
	struct virtio_dev_part parts[];
};

#define VIRTIO_ADMIN_CMD_DEV_MODE_F_STOPPED 0

struct virtio_admin_cmd_dev_mode_set_data {
	uint8_t flags;
};

#endif
