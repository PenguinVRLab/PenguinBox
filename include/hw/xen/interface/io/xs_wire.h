/* SPDX-License-Identifier: MIT */
/*
 * Details of the "wire" protocol between Xen Store Daemon and client
 * library or guest kernel.
 *
 * Copyright (C) 2005 Rusty Russell IBM Corporation
 */

#ifndef _XS_WIRE_H
#define _XS_WIRE_H

enum xsd_sockmsg_type
{
    XS_CONTROL,
#define XS_DEBUG XS_CONTROL
    XS_DIRECTORY,
    XS_READ,
    XS_GET_PERMS,
    XS_WATCH,
    XS_UNWATCH,
    XS_TRANSACTION_START,
    XS_TRANSACTION_END,
    XS_INTRODUCE,
    XS_RELEASE,
    XS_GET_DOMAIN_PATH,
    XS_WRITE,
    XS_MKDIR,
    XS_RM,
    XS_SET_PERMS,
    XS_WATCH_EVENT,
    XS_ERROR,
    XS_IS_DOMAIN_INTRODUCED,
    XS_RESUME,
    XS_SET_TARGET,
    XS_RESET_WATCHES = XS_SET_TARGET + 2,
    XS_DIRECTORY_PART,

    XS_TYPE_COUNT,

    XS_INVALID = 0xffff
};

#define XS_WRITE_NONE "NONE"
#define XS_WRITE_CREATE "CREATE"
#define XS_WRITE_CREATE_EXCL "CREATE|EXCL"

struct xsd_errors
{
    int errnum;
    const char *errstring;
};
#ifdef EINVAL
#define XSD_ERROR(x) { x, #x }
static const struct xsd_errors xsd_errors[]
#if defined(__GNUC__)
__attribute__((unused))
#endif
    = {
    XSD_ERROR(EINVAL),
    XSD_ERROR(EACCES),
    XSD_ERROR(EEXIST),
    XSD_ERROR(EISDIR),
    XSD_ERROR(ENOENT),
    XSD_ERROR(ENOMEM),
    XSD_ERROR(ENOSPC),
    XSD_ERROR(EIO),
    XSD_ERROR(ENOTEMPTY),
    XSD_ERROR(ENOSYS),
    XSD_ERROR(EROFS),
    XSD_ERROR(EBUSY),
    XSD_ERROR(EAGAIN),
    XSD_ERROR(EISCONN),
    XSD_ERROR(E2BIG),
    XSD_ERROR(EPERM),
};
#endif

struct xsd_sockmsg
{
    uint32_t type;
    uint32_t req_id;
    uint32_t tx_id;
    uint32_t len;

};

enum xs_watch_type
{
    XS_WATCH_PATH = 0,
    XS_WATCH_TOKEN
};

#define XENSTORE_RING_SIZE 1024
typedef uint32_t XENSTORE_RING_IDX;
#define MASK_XENSTORE_IDX(idx) ((idx) & (XENSTORE_RING_SIZE-1))
struct xenstore_domain_interface {
    char req[XENSTORE_RING_SIZE];
    char rsp[XENSTORE_RING_SIZE];
    XENSTORE_RING_IDX req_cons, req_prod;
    XENSTORE_RING_IDX rsp_cons, rsp_prod;
    uint32_t server_features;
    uint32_t connection;
    uint32_t error;
};

#define XENSTORE_PAYLOAD_MAX 4096

#define XENSTORE_ABS_PATH_MAX 3072
#define XENSTORE_REL_PATH_MAX 2048

#define XENSTORE_SERVER_FEATURE_RECONNECTION 1
#define XENSTORE_SERVER_FEATURE_ERROR        2

#define XENSTORE_CONNECTED 0
#define XENSTORE_RECONNECT 1

#define XENSTORE_ERROR_NONE    0
#define XENSTORE_ERROR_COMM    1
#define XENSTORE_ERROR_RINGIDX 2
#define XENSTORE_ERROR_PROTO   3

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
