/*
 * tpm_ioctl.h
 *
 * (c) Copyright IBM Corporation 2014, 2015.
 *
 * This file is licensed under the terms of the 3-clause BSD license
 */
#ifndef _TPM_IOCTL_H_
#define _TPM_IOCTL_H_

#if defined(__CYGWIN__)
# define __USE_LINUX_IOCTL_DEFS
#endif

#ifndef _WIN32
#include <sys/uio.h>
#include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_IOCCOM_H
#include <sys/ioccom.h>
#endif

typedef uint32_t ptm_res;

struct ptm_cap_n {
    union {
        struct {
            ptm_res tpm_result;
            uint32_t caps;
        } resp;
    } u;
};

struct ptm_est {
    union {
        struct {
            ptm_res tpm_result;
            unsigned char bit;
        } resp;
    } u;
};

struct ptm_reset_est {
    union {
        struct {
            uint8_t loc;
        } req;
        struct {
            ptm_res tpm_result;
        } resp;
    } u;
};

struct ptm_init {
    union {
        struct {
            uint32_t init_flags;
        } req;
        struct {
            ptm_res tpm_result;
        } resp;
    } u;
};

#define PTM_INIT_FLAG_DELETE_VOLATILE (1 << 0)

struct ptm_loc {
    union {
        struct {
            uint8_t loc;
        } req;
        struct {
            ptm_res tpm_result;
        } resp;
    } u;
};

struct ptm_hdata {
    union {
        struct {
            uint32_t length;
            uint8_t data[4096];
        } req;
        struct {
            ptm_res tpm_result;
        } resp;
    } u;
};

#define PTM_STATE_BLOB_SIZE (3 * 1024)

struct ptm_getstate {
    union {
        struct {
            uint32_t state_flags;
            uint32_t type;
            uint32_t offset;
        } req;
        struct {
            ptm_res tpm_result;
            uint32_t state_flags;
            uint32_t totlength;
            uint32_t length;
            uint8_t  data[PTM_STATE_BLOB_SIZE];
        } resp;
    } u;
};

#define PTM_BLOB_TYPE_PERMANENT  1
#define PTM_BLOB_TYPE_VOLATILE   2
#define PTM_BLOB_TYPE_SAVESTATE  3

#define PTM_STATE_FLAG_DECRYPTED     1
#define PTM_STATE_FLAG_ENCRYPTED     2

struct ptm_setstate {
    union {
        struct {
            uint32_t state_flags;
            uint32_t type;
            uint32_t length;
            uint8_t data[PTM_STATE_BLOB_SIZE];
        } req;
        struct {
            ptm_res tpm_result;
        } resp;
    } u;
};

struct ptm_getconfig {
    union {
        struct {
            ptm_res tpm_result;
            uint32_t flags;
        } resp;
    } u;
};

#define PTM_CONFIG_FLAG_FILE_KEY        0x1
#define PTM_CONFIG_FLAG_MIGRATION_KEY   0x2

struct ptm_setbuffersize {
    union {
        struct {
            uint32_t buffersize;
        } req;
        struct {
            ptm_res tpm_result;
            uint32_t buffersize;
            uint32_t minsize;
            uint32_t maxsize;
        } resp;
    } u;
};

#define PTM_GETINFO_SIZE (3 * 1024)
struct ptm_getinfo {
    union {
        struct {
            uint64_t flags;
            uint32_t offset;
            uint32_t pad;
        } req;
        struct {
            ptm_res tpm_result;
            uint32_t totlength;
            uint32_t length;
            char buffer[PTM_GETINFO_SIZE];
        } resp;
    } u;
};

#define SWTPM_INFO_TPMSPECIFICATION ((uint64_t)1 << 0)
#define SWTPM_INFO_TPMATTRIBUTES    ((uint64_t)1 << 1)

struct ptm_lockstorage {
    union {
        struct {
            uint32_t retries;
        } req;
        struct {
            ptm_res tpm_result;
        } resp;
    } u;
};

typedef uint64_t ptm_cap;
typedef struct ptm_cap_n ptm_cap_n;
typedef struct ptm_est ptm_est;
typedef struct ptm_reset_est ptm_reset_est;
typedef struct ptm_loc ptm_loc;
typedef struct ptm_hdata ptm_hdata;
typedef struct ptm_init ptm_init;
typedef struct ptm_getstate ptm_getstate;
typedef struct ptm_setstate ptm_setstate;
typedef struct ptm_getconfig ptm_getconfig;
typedef struct ptm_setbuffersize ptm_setbuffersize;
typedef struct ptm_getinfo ptm_getinfo;
typedef struct ptm_lockstorage ptm_lockstorage;

#define PTM_CAP_INIT               (1)
#define PTM_CAP_SHUTDOWN           (1 << 1)
#define PTM_CAP_GET_TPMESTABLISHED (1 << 2)
#define PTM_CAP_SET_LOCALITY       (1 << 3)
#define PTM_CAP_HASHING            (1 << 4)
#define PTM_CAP_CANCEL_TPM_CMD     (1 << 5)
#define PTM_CAP_STORE_VOLATILE     (1 << 6)
#define PTM_CAP_RESET_TPMESTABLISHED (1 << 7)
#define PTM_CAP_GET_STATEBLOB      (1 << 8)
#define PTM_CAP_SET_STATEBLOB      (1 << 9)
#define PTM_CAP_STOP               (1 << 10)
#define PTM_CAP_GET_CONFIG         (1 << 11)
#define PTM_CAP_SET_DATAFD         (1 << 12)
#define PTM_CAP_SET_BUFFERSIZE     (1 << 13)
#define PTM_CAP_GET_INFO           (1 << 14)
#define PTM_CAP_SEND_COMMAND_HEADER (1 << 15)
#define PTM_CAP_LOCK_STORAGE       (1 << 16)

#ifndef _WIN32
enum {
    PTM_GET_CAPABILITY     = _IOR('P', 0, ptm_cap),
    PTM_INIT               = _IOWR('P', 1, ptm_init),
    PTM_SHUTDOWN           = _IOR('P', 2, ptm_res),
    PTM_GET_TPMESTABLISHED = _IOR('P', 3, ptm_est),
    PTM_SET_LOCALITY       = _IOWR('P', 4, ptm_loc),
    PTM_HASH_START         = _IOR('P', 5, ptm_res),
    PTM_HASH_DATA          = _IOWR('P', 6, ptm_hdata),
    PTM_HASH_END           = _IOR('P', 7, ptm_res),
    PTM_CANCEL_TPM_CMD     = _IOR('P', 8, ptm_res),
    PTM_STORE_VOLATILE     = _IOR('P', 9, ptm_res),
    PTM_RESET_TPMESTABLISHED = _IOWR('P', 10, ptm_reset_est),
    PTM_GET_STATEBLOB      = _IOWR('P', 11, ptm_getstate),
    PTM_SET_STATEBLOB      = _IOWR('P', 12, ptm_setstate),
    PTM_STOP               = _IOR('P', 13, ptm_res),
    PTM_GET_CONFIG         = _IOR('P', 14, ptm_getconfig),
    PTM_SET_DATAFD         = _IOR('P', 15, ptm_res),
    PTM_SET_BUFFERSIZE     = _IOWR('P', 16, ptm_setbuffersize),
    PTM_GET_INFO           = _IOWR('P', 17, ptm_getinfo),
    PTM_LOCK_STORAGE       = _IOWR('P', 18, ptm_lockstorage),
};
#endif

enum {
    CMD_GET_CAPABILITY = 1,
    CMD_INIT,
    CMD_SHUTDOWN,
    CMD_GET_TPMESTABLISHED,
    CMD_SET_LOCALITY,
    CMD_HASH_START,
    CMD_HASH_DATA,
    CMD_HASH_END,
    CMD_CANCEL_TPM_CMD,
    CMD_STORE_VOLATILE,
    CMD_RESET_TPMESTABLISHED,
    CMD_GET_STATEBLOB,
    CMD_SET_STATEBLOB,
    CMD_STOP,
    CMD_GET_CONFIG,
    CMD_SET_DATAFD,
    CMD_SET_BUFFERSIZE,
    CMD_GET_INFO,
    CMD_LOCK_STORAGE,
};

#endif
