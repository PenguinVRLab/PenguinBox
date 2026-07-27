/*
 * Virtio driver bits
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef VIRTIO_H
#define VIRTIO_H

#define VIRTIO_CONFIG_S_ACKNOWLEDGE     1
#define VIRTIO_CONFIG_S_DRIVER          2
#define VIRTIO_CONFIG_S_DRIVER_OK       4
#define VIRTIO_CONFIG_S_FAILED          0x80

enum VirtioDevType {
    VIRTIO_ID_NET = 1,
    VIRTIO_ID_BLOCK = 2,
    VIRTIO_ID_CONSOLE = 3,
    VIRTIO_ID_BALLOON = 5,
    VIRTIO_ID_SCSI = 8,
};
typedef enum VirtioDevType VirtioDevType;

struct VqInfo {
    uint64_t queue;
    uint32_t align;
    uint16_t index;
    uint16_t num;
} __attribute__((packed));
typedef struct VqInfo VqInfo;

struct VqConfig {
    uint16_t index;
    uint16_t num;
} __attribute__((packed));
typedef struct VqConfig VqConfig;

#define VIRTIO_RING_SIZE            (PAGE_SIZE * 8)
#define VIRTIO_MAX_VQS              3
#define KVM_S390_VIRTIO_RING_ALIGN  4096

#define VRING_USED_F_NO_NOTIFY  1

#define VRING_DESC_F_NEXT       1
#define VRING_DESC_F_WRITE      2
#define VRING_DESC_F_INDIRECT   4

#define VRING_HIDDEN_IS_CHAIN   256

struct VRingDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));
typedef struct VRingDesc VRingDesc;

struct VRingAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));
typedef struct VRingAvail VRingAvail;

struct VRingUsedElem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));
typedef struct VRingUsedElem VRingUsedElem;

struct VRingUsed {
    uint16_t flags;
    uint16_t idx;
    VRingUsedElem ring[];
} __attribute__((packed));
typedef struct VRingUsed VRingUsed;

struct VRing {
    unsigned int num;
    int next_idx;
    int used_idx;
    VRingDesc *desc;
    VRingAvail *avail;
    VRingUsed *used;
    SubChannelId schid;
    long cookie;
    int id;
};
typedef struct VRing VRing;


#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1

#define VIRTIO_BLK_T_SCSI_CMD   2

#define VIRTIO_BLK_T_FLUSH      4

#define VIRTIO_BLK_T_BARRIER    0x80000000

struct VirtioBlkOuthdr {
        uint32_t type;
        uint32_t ioprio;
        uint64_t sector;
};
typedef struct VirtioBlkOuthdr VirtioBlkOuthdr;

struct VirtioBlkConfig {
    uint64_t capacity;
    uint32_t size_max;
    uint32_t seg_max;

    struct VirtioBlkGeometry {
        uint16_t cylinders;
        uint8_t heads;
        uint8_t sectors;
    } geometry;

    uint32_t blk_size;

    uint8_t physical_block_exp;
    uint8_t alignment_offset;
    uint16_t min_io_size;
    uint32_t opt_io_size;

    uint8_t wce;
} __attribute__((packed));
typedef struct VirtioBlkConfig VirtioBlkConfig;

enum guessed_disk_nature_type {
    VIRTIO_GDN_NONE     = 0,
    VIRTIO_GDN_DASD     = 1,
    VIRTIO_GDN_CDROM    = 2,
    VIRTIO_GDN_SCSI     = 3,
};
typedef enum guessed_disk_nature_type VirtioGDN;

VirtioGDN virtio_guessed_disk_nature(void);
void virtio_assume_eckd(void);
void virtio_assume_iso9660(void);

bool virtio_ipl_disk_is_valid(void);
int virtio_get_block_size(void);
uint8_t virtio_get_heads(void);
uint8_t virtio_get_sectors(void);
uint64_t virtio_get_blocks(void);
int virtio_read_many(unsigned long sector, void *load_addr, int sec_num);

#define VIRTIO_SECTOR_SIZE 512
#define VIRTIO_ISO_BLOCK_SIZE 2048
#define VIRTIO_SCSI_BLOCK_SIZE 512
#define VIRTIO_DASD_DEFAULT_BLOCK_SIZE 4096

static inline unsigned long virtio_sector_adjust(unsigned long sector)
{
    return sector * (virtio_get_block_size() / VIRTIO_SECTOR_SIZE);
}

struct VirtioScsiConfig {
    uint32_t num_queues;
    uint32_t seg_max;
    uint32_t max_sectors;
    uint32_t cmd_per_lun;
    uint32_t event_info_size;
    uint32_t sense_size;
    uint32_t cdb_size;
    uint16_t max_channel;
    uint16_t max_target;
    uint32_t max_lun;
} __attribute__((packed));
typedef struct VirtioScsiConfig VirtioScsiConfig;

struct ScsiDevice {
    uint16_t channel;
    uint16_t target;
    uint32_t lun;
};
typedef struct ScsiDevice ScsiDevice;

struct VirtioNetConfig {
    uint8_t mac[6];
};
typedef struct VirtioNetConfig VirtioNetConfig;

struct VDev {
    int nr_vqs;
    VRing *vrings;
    int cmd_vr_idx;
    void *ring_area;
    long wait_reply_timeout;
    VirtioGDN guessed_disk_nature;
    SubChannelId schid;
    SenseId senseid;
    union {
        VirtioBlkConfig blk;
        VirtioScsiConfig scsi;
        VirtioNetConfig net;
    } config;
    ScsiDevice *scsi_device;
    bool is_cdrom;
    int scsi_block_size;
    int blk_factor;
    uint64_t scsi_last_block;
    uint32_t scsi_dev_cyls;
    uint8_t scsi_dev_heads;
    bool scsi_device_selected;
    ScsiDevice selected_scsi_device;
    uint32_t max_transfer;
    uint32_t guest_features[2];
};
typedef struct VDev VDev;

VDev *virtio_get_device(void);
VirtioDevType virtio_get_device_type(void);

struct VirtioCmd {
    void *data;
    int size;
    int flags;
};
typedef struct VirtioCmd VirtioCmd;

bool vring_notify(VRing *vr);
int drain_irqs(SubChannelId schid);
void vring_send_buf(VRing *vr, void *p, int len, int flags);
int vr_poll(VRing *vr);
int vring_wait_reply(void);
int virtio_run(VDev *vdev, int vqid, VirtioCmd *cmd);
int virtio_reset(VDev *vdev);
int virtio_setup_ccw(VDev *vdev);

int virtio_net_init(void *mac_addr);
void virtio_net_deinit(void);

#endif
