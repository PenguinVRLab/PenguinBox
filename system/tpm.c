/*
 * TPM configuration
 *
 * Copyright (C) 2011-2013 IBM Corporation
 *
 * Authors:
 *  Stefan Berger    <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Based on net.c
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/qapi-commands-tpm.h"
#include "qapi/qmp/qerror.h"
#include "system/tpm_backend.h"
#include "system/tpm.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/help_option.h"

static QLIST_HEAD(, TPMBackend) tpm_backends =
    QLIST_HEAD_INITIALIZER(tpm_backends);

static const TPMBackendClass *
tpm_be_find_by_type(enum TpmType type)
{
    ObjectClass *oc;
    char *typename = g_strdup_printf("tpm-%s", TpmType_str(type));

    oc = object_class_by_name(typename);
    g_free(typename);

    if (!object_class_dynamic_cast(oc, TYPE_TPM_BACKEND)) {
        return NULL;
    }

    return TPM_BACKEND_CLASS(oc);
}

static void tpm_display_backend_drivers(void)
{
    bool got_one = false;
    int i;

    for (i = 0; i < TPM_TYPE__MAX; i++) {
        const TPMBackendClass *bc = tpm_be_find_by_type(i);
        if (!bc) {
            continue;
        }
        if (!got_one) {
            error_printf("Supported TPM types (choose only one):\n");
            got_one = true;
        }
        error_printf("%12s   %s\n", TpmType_str(i), bc->desc);
    }
    if (!got_one) {
        error_printf("No TPM backend types are available\n");
    }
}

TPMBackend *qemu_find_tpm_be(const char *id)
{
    TPMBackend *drv;

    if (id) {
        QLIST_FOREACH(drv, &tpm_backends, list) {
            if (!strcmp(drv->id, id)) {
                return drv;
            }
        }
    }

    return NULL;
}

static int tpm_init_tpmdev(void *dummy, QemuOpts *opts, Error **errp)
{
    const char *value;
    const char *id;
    const TPMBackendClass *be;
    TPMBackend *drv;
    Error *local_err = NULL;
    int i;

    if (!QLIST_EMPTY(&tpm_backends)) {
        error_report("Only one TPM is allowed.");
        return 1;
    }

    id = qemu_opts_id(opts);
    if (id == NULL) {
        error_report(QERR_MISSING_PARAMETER, "id");
        return 1;
    }

    value = qemu_opt_get(opts, "type");
    if (!value) {
        error_report(QERR_MISSING_PARAMETER, "type");
        tpm_display_backend_drivers();
        return 1;
    }

    i = qapi_enum_parse(&TpmType_lookup, value, -1, NULL);
    be = i >= 0 ? tpm_be_find_by_type(i) : NULL;
    if (be == NULL) {
        error_report(QERR_INVALID_PARAMETER_VALUE,
                     "type", "a TPM backend type");
        tpm_display_backend_drivers();
        return 1;
    }

    if (!qemu_opts_validate(opts, be->opts, &local_err)) {
        error_report_err(local_err);
        return 1;
    }

    drv = be->create(opts);
    if (!drv) {
        return 1;
    }

    drv->id = g_strdup(id);
    QLIST_INSERT_HEAD(&tpm_backends, drv, list);

    return 0;
}

void tpm_cleanup(void)
{
    TPMBackend *drv, *next;

    QLIST_FOREACH_SAFE(drv, &tpm_backends, list, next) {
        QLIST_REMOVE(drv, list);
        object_unref(OBJECT(drv));
    }
}

int tpm_init(void)
{
    if (qemu_opts_foreach(qemu_find_opts("tpmdev"),
                          tpm_init_tpmdev, NULL, NULL)) {
        return -1;
    }

    return 0;
}

int tpm_config_parse(QemuOptsList *opts_list, const char *optstr)
{
    QemuOpts *opts;

    if (is_help_option(optstr)) {
        tpm_display_backend_drivers();
        exit(EXIT_SUCCESS);
    }
    opts = qemu_opts_parse_noisily(opts_list, optstr, true);
    if (!opts) {
        return -1;
    }
    return 0;
}

TPMInfoList *qmp_query_tpm(Error **errp)
{
    TPMBackend *drv;
    TPMInfoList *head = NULL, **tail = &head;

    QLIST_FOREACH(drv, &tpm_backends, list) {
        if (!drv->tpmif) {
            continue;
        }

        QAPI_LIST_APPEND(tail, tpm_backend_query_tpm(drv));
    }

    return head;
}

TpmTypeList *qmp_query_tpm_types(Error **errp)
{
    unsigned int i = 0;
    TpmTypeList *head = NULL, **tail = &head;

    for (i = 0; i < TPM_TYPE__MAX; i++) {
        if (!tpm_be_find_by_type(i)) {
            continue;
        }
        QAPI_LIST_APPEND(tail, i);
    }

    return head;
}
TpmModelList *qmp_query_tpm_models(Error **errp)
{
    TpmModelList *head = NULL, **tail = &head;
    GSList *e, *l = object_class_get_list(TYPE_TPM_IF, false);

    for (e = l; e; e = e->next) {
        TPMIfClass *c = TPM_IF_CLASS(e->data);

        QAPI_LIST_APPEND(tail, c->model);
    }
    g_slist_free(l);

    return head;
}
