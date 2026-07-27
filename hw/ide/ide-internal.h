#ifndef HW_IDE_INTERNAL_H
#define HW_IDE_INTERNAL_H

#include "hw/ide/ide-bus.h"

#define USE_DMA_CDROM

#define ATA_DEV_SELECT          0x10
#define ATA_DEV_ALWAYS_ON       0xA0
#define ATA_DEV_LBA             0x40
#define ATA_DEV_LBA_MSB         0x0F
#define ATA_DEV_HS              0x0F


#define ERR_STAT                0x01
#define INDEX_STAT              0x02
#define ECC_STAT                0x04
#define DRQ_STAT                0x08
#define SEEK_STAT               0x10
#define SRV_STAT                0x10
#define WRERR_STAT              0x20
#define READY_STAT              0x40
#define BUSY_STAT               0x80

#define MARK_ERR                0x01
#define TRK0_ERR                0x02
#define ABRT_ERR                0x04
#define MCR_ERR                 0x08
#define ID_ERR                  0x10
#define MC_ERR                  0x20
#define ECC_ERR                 0x40
#define BBD_ERR                 0x80
#define ICRC_ERR                0x80

#define CD                      0x01
#define IO                      0x02
#define REL                     0x04
#define TAG_MASK                0xf8

#define IDE_CTRL_HOB            0x80
#define IDE_CTRL_RESET          0x04
#define IDE_CTRL_DISABLE_IRQ    0x02

#define WIN_NOP                         0x00
#define CFA_REQ_EXT_ERROR_CODE          0x03
#define WIN_DSM                         0x06
#define WIN_DEVICE_RESET                0x08
#define WIN_RECAL                       0x10
#define WIN_READ                        0x20
#define WIN_READ_ONCE                   0x21
#define WIN_READ_EXT                    0x24
#define WIN_READDMA_EXT                 0x25
#define WIN_READDMA_QUEUED_EXT          0x26
#define WIN_READ_NATIVE_MAX_EXT         0x27
#define WIN_MULTREAD_EXT                0x29
#define WIN_WRITE                       0x30
#define WIN_WRITE_ONCE                  0x31
#define WIN_WRITE_EXT                   0x34
#define WIN_WRITEDMA_EXT                0x35
#define WIN_WRITEDMA_QUEUED_EXT         0x36
#define WIN_SET_MAX_EXT                 0x37
#define WIN_SET_MAX_EXT                 0x37
#define CFA_WRITE_SECT_WO_ERASE         0x38
#define WIN_MULTWRITE_EXT               0x39
#define WIN_WRITE_VERIFY                0x3C
#define WIN_VERIFY                      0x40
#define WIN_VERIFY_ONCE                 0x41
#define WIN_VERIFY_EXT                  0x42
#define WIN_SEEK                        0x70
#define CFA_TRANSLATE_SECTOR            0x87
#define WIN_DIAGNOSE                    0x90
#define WIN_SPECIFY                     0x91
#define WIN_DOWNLOAD_MICROCODE          0x92
#define WIN_STANDBYNOW2                 0x94
#define WIN_IDLEIMMEDIATE2              0x95
#define WIN_STANDBY2                    0x96
#define WIN_SETIDLE2                    0x97
#define WIN_CHECKPOWERMODE2             0x98
#define WIN_SLEEPNOW2                   0x99
#define WIN_PACKETCMD                   0xA0
#define WIN_PIDENTIFY                   0xA1
#define WIN_QUEUED_SERVICE              0xA2
#define WIN_SMART                       0xB0
#define CFA_ACCESS_METADATA_STORAGE     0xB8
#define CFA_ERASE_SECTORS               0xC0
#define WIN_MULTREAD                    0xC4
#define WIN_MULTWRITE                   0xC5
#define WIN_SETMULT                     0xC6
#define WIN_READDMA_QUEUED              0xC7
#define WIN_READDMA                     0xC8
#define WIN_READDMA_ONCE                0xC9
#define WIN_WRITEDMA                    0xCA
#define WIN_WRITEDMA_ONCE               0xCB
#define WIN_WRITEDMA_QUEUED             0xCC
#define CFA_WRITE_MULTI_WO_ERASE        0xCD
#define WIN_GETMEDIASTATUS              0xDA
#define WIN_DOORLOCK                    0xDE
#define WIN_DOORUNLOCK                  0xDF
#define WIN_STANDBYNOW1                 0xE0
#define WIN_IDLEIMMEDIATE               0xE1
#define WIN_STANDBY                     0xE2
#define WIN_SETIDLE1                    0xE3
#define WIN_READ_BUFFER                 0xE4
#define WIN_CHECKPOWERMODE1             0xE5
#define WIN_SLEEPNOW1                   0xE6
#define WIN_FLUSH_CACHE                 0xE7
#define WIN_WRITE_BUFFER                0xE8
#define WIN_FLUSH_CACHE_EXT             0xEA
#define WIN_IDENTIFY                    0xEC
#define WIN_MEDIAEJECT                  0xED
#define WIN_SETFEATURES                 0xEF
#define IBM_SENSE_CONDITION             0xF0
#define WIN_SECURITY_SET_PASS           0xF1
#define WIN_SECURITY_UNLOCK             0xF2
#define WIN_SECURITY_ERASE_PREPARE      0xF3
#define WIN_SECURITY_ERASE_UNIT         0xF4
#define WIN_SECURITY_FREEZE_LOCK        0xF5
#define CFA_WEAR_LEVEL                  0xF5
#define WIN_SECURITY_DISABLE            0xF6
#define WIN_READ_NATIVE_MAX             0xF8
#define WIN_SET_MAX                     0xF9

#define MAX_MULT_SECTORS 16

#define IDE_DMA_BUF_SECTORS 256

#define DSM_TRIM                        0x01

#if (IDE_DMA_BUF_SECTORS < MAX_MULT_SECTORS)
#error "IDE_DMA_BUF_SECTORS must be bigger or equal to MAX_MULT_SECTORS"
#endif

#define ATAPI_PACKET_SIZE 12

#define GPCMD_BLANK                         0xa1
#define GPCMD_CLOSE_TRACK                   0x5b
#define GPCMD_FLUSH_CACHE                   0x35
#define GPCMD_FORMAT_UNIT                   0x04
#define GPCMD_GET_CONFIGURATION             0x46
#define GPCMD_GET_EVENT_STATUS_NOTIFICATION 0x4a
#define GPCMD_GET_PERFORMANCE               0xac
#define GPCMD_INQUIRY                       0x12
#define GPCMD_LOAD_UNLOAD                   0xa6
#define GPCMD_MECHANISM_STATUS              0xbd
#define GPCMD_MODE_SELECT_10                0x55
#define GPCMD_MODE_SENSE_10                 0x5a
#define GPCMD_PAUSE_RESUME                  0x4b
#define GPCMD_PLAY_AUDIO_10                 0x45
#define GPCMD_PLAY_AUDIO_MSF                0x47
#define GPCMD_PLAY_AUDIO_TI                 0x48
#define GPCMD_PLAY_CD                       0xbc
#define GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL  0x1e
#define GPCMD_READ_10                       0x28
#define GPCMD_READ_12                       0xa8
#define GPCMD_READ_CDVD_CAPACITY            0x25
#define GPCMD_READ_CD                       0xbe
#define GPCMD_READ_CD_MSF                   0xb9
#define GPCMD_READ_DISC_INFO                0x51
#define GPCMD_READ_DVD_STRUCTURE            0xad
#define GPCMD_READ_FORMAT_CAPACITIES        0x23
#define GPCMD_READ_HEADER                   0x44
#define GPCMD_READ_TRACK_RZONE_INFO         0x52
#define GPCMD_READ_SUBCHANNEL               0x42
#define GPCMD_READ_TOC_PMA_ATIP             0x43
#define GPCMD_REPAIR_RZONE_TRACK            0x58
#define GPCMD_REPORT_KEY                    0xa4
#define GPCMD_REQUEST_SENSE                 0x03
#define GPCMD_RESERVE_RZONE_TRACK           0x53
#define GPCMD_SCAN                          0xba
#define GPCMD_SEEK                          0x2b
#define GPCMD_SEND_DVD_STRUCTURE            0xad
#define GPCMD_SEND_EVENT                    0xa2
#define GPCMD_SEND_KEY                      0xa3
#define GPCMD_SEND_OPC                      0x54
#define GPCMD_SET_READ_AHEAD                0xa7
#define GPCMD_SET_STREAMING                 0xb6
#define GPCMD_START_STOP_UNIT               0x1b
#define GPCMD_STOP_PLAY_SCAN                0x4e
#define GPCMD_TEST_UNIT_READY               0x00
#define GPCMD_VERIFY_10                     0x2f
#define GPCMD_WRITE_10                      0x2a
#define GPCMD_WRITE_AND_VERIFY_10           0x2e
#define GPCMD_SET_SPEED                     0xbb
#define GPCMD_PLAYAUDIO_TI                  0x48
#define GPCMD_GET_MEDIA_STATUS              0xda
#define GPCMD_MODE_SENSE_6                  0x1a

#define ATAPI_INT_REASON_CD             0x01
#define ATAPI_INT_REASON_IO             0x02
#define ATAPI_INT_REASON_REL            0x04
#define ATAPI_INT_REASON_TAG            0xf8

#define ASC_NO_SEEK_COMPLETE                 0x02
#define ASC_ILLEGAL_OPCODE                   0x20
#define ASC_LOGICAL_BLOCK_OOR                0x21
#define ASC_INV_FIELD_IN_CMD_PACKET          0x24
#define ASC_MEDIUM_MAY_HAVE_CHANGED          0x28
#define ASC_INCOMPATIBLE_FORMAT              0x30
#define ASC_MEDIUM_NOT_PRESENT               0x3a
#define ASC_SAVING_PARAMETERS_NOT_SUPPORTED  0x39
#define ASC_DATA_PHASE_ERROR                 0x4b
#define ASC_MEDIA_REMOVAL_PREVENTED          0x53

#define CFA_NO_ERROR            0x00
#define CFA_MISC_ERROR          0x09
#define CFA_INVALID_COMMAND     0x20
#define CFA_INVALID_ADDRESS     0x21
#define CFA_ADDRESS_OVERFLOW    0x2f

#define SMART_READ_DATA       0xd0
#define SMART_READ_THRESH     0xd1
#define SMART_ATTR_AUTOSAVE   0xd2
#define SMART_SAVE_ATTR       0xd3
#define SMART_EXECUTE_OFFLINE 0xd4
#define SMART_READ_LOG        0xd5
#define SMART_WRITE_LOG       0xd6
#define SMART_ENABLE          0xd8
#define SMART_DISABLE         0xd9
#define SMART_STATUS          0xda

extern const char *IDE_DMA_CMD_lookup[IDE_DMA__COUNT];

extern const MemoryRegionPortio ide_portio_list[];
extern const MemoryRegionPortio ide_portio2_list[];

#define ide_cmd_is_read(s) \
        ((s)->dma_cmd == IDE_DMA_READ)

typedef struct IDEBufferedRequest {
    QLIST_ENTRY(IDEBufferedRequest) list;
    QEMUIOVector qiov;
    QEMUIOVector *original_qiov;
    BlockCompletionFunc *original_cb;
    void *original_opaque;
    bool orphaned;
} IDEBufferedRequest;

#define IDE_RETRY_MASK 0xf8
#define IDE_RETRY_DMA  0x08
#define IDE_RETRY_PIO  0x10
#define IDE_RETRY_ATAPI 0x20
#define IDE_RETRY_READ  0x20
#define IDE_RETRY_FLUSH 0x40
#define IDE_RETRY_TRIM 0x80
#define IDE_RETRY_HBA  0x100

#define IS_IDE_RETRY_DMA(_status) \
    ((_status) & IDE_RETRY_DMA)

#define IS_IDE_RETRY_PIO(_status) \
    ((_status) & IDE_RETRY_PIO)

#define IS_IDE_RETRY_ATAPI(_status)   \
    (((_status) & IDE_RETRY_MASK) == IDE_RETRY_ATAPI)

static inline uint8_t ide_dma_cmd_to_retry(uint8_t dma_cmd)
{
    switch (dma_cmd) {
    case IDE_DMA_READ:
        return IDE_RETRY_DMA | IDE_RETRY_READ;
    case IDE_DMA_WRITE:
        return IDE_RETRY_DMA;
    case IDE_DMA_TRIM:
        return IDE_RETRY_DMA | IDE_RETRY_TRIM;
    case IDE_DMA_ATAPI:
        return IDE_RETRY_ATAPI;
    default:
        break;
    }
    return 0;
}

static inline IDEState *ide_bus_active_if(IDEBus *bus)
{
    return bus->ifs + bus->unit;
}

extern const VMStateDescription vmstate_ide_bus;

#define VMSTATE_IDE_BUS(_field, _state)                          \
    VMSTATE_STRUCT(_field, _state, 1, vmstate_ide_bus, IDEBus)

#define VMSTATE_IDE_BUS_ARRAY(_field, _state, _num)              \
    VMSTATE_STRUCT_ARRAY(_field, _state, _num, 1, vmstate_ide_bus, IDEBus)

extern const VMStateDescription vmstate_ide_drive;

#define VMSTATE_IDE_DRIVES(_field, _state) \
    VMSTATE_STRUCT_ARRAY(_field, _state, 2, 3, vmstate_ide_drive, IDEState)

#define VMSTATE_IDE_DRIVE(_field, _state) \
    VMSTATE_STRUCT(_field, _state, 1, vmstate_ide_drive, IDEState)

void ide_bus_reset(IDEBus *bus);
int64_t ide_get_sector(IDEState *s);
void ide_set_sector(IDEState *s, int64_t sector_num);

void ide_start_dma(IDEState *s, BlockCompletionFunc *cb);
void ide_dma_buf_commit(IDEState *s, uint32_t tx_bytes);
void ide_dma_error(IDEState *s);
void ide_abort_command(IDEState *s);

void ide_atapi_cmd_ok(IDEState *s);
void ide_atapi_cmd_error(IDEState *s, int sense_key, int asc);
void ide_atapi_dma_restart(IDEState *s);
void ide_atapi_io_error(IDEState *s, int ret);

void ide_ioport_write(void *opaque, uint32_t addr, uint32_t val);
uint32_t ide_ioport_read(void *opaque, uint32_t addr1);
uint32_t ide_status_read(void *opaque, uint32_t addr);
void ide_ctrl_write(void *opaque, uint32_t addr, uint32_t val);
void ide_data_writew(void *opaque, uint32_t addr, uint32_t val);
uint32_t ide_data_readw(void *opaque, uint32_t addr);
void ide_data_writel(void *opaque, uint32_t addr, uint32_t val);
uint32_t ide_data_readl(void *opaque, uint32_t addr);

int ide_init_drive(IDEState *s, IDEDevice *dev, IDEDriveKind kind, Error **errp);
void ide_exit(IDEState *s);
void ide_bus_init_output_irq(IDEBus *bus, qemu_irq irq_out);
int ide_init_ioport(IDEBus *bus, ISADevice *isa, int iobase, int iobase2);
void ide_bus_set_irq(IDEBus *bus);
void ide_bus_register_restart_cb(IDEBus *bus);

void ide_bus_exec_cmd(IDEBus *bus, uint32_t val);

void ide_transfer_start(IDEState *s, uint8_t *buf, int size,
                        EndTransferFunc *end_transfer_func);
bool ide_transfer_start_norecurse(IDEState *s, uint8_t *buf, int size,
                                  EndTransferFunc *end_transfer_func);
void ide_transfer_stop(IDEState *s);
void ide_set_inactive(IDEState *s, bool more);
BlockAIOCB *ide_issue_trim(
        int64_t offset, QEMUIOVector *qiov,
        BlockCompletionFunc *cb, void *cb_opaque, void *opaque);
BlockAIOCB *ide_buffered_readv(IDEState *s, int64_t sector_num,
                               QEMUIOVector *iov, int nb_sectors,
                               BlockCompletionFunc *cb, void *opaque);
void ide_cancel_dma_sync(IDEState *s);

void ide_atapi_cmd(IDEState *s);
void ide_atapi_cmd_reply_end(IDEState *s);

int ide_handle_rw_error(IDEState *s, int error, int op);

#endif
