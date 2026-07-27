/*
 * QEMU VNC display driver: SASL auth protocol
 *
 * Copyright (C) 2009 Red Hat, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_VNC_AUTH_SASL_H
#define QEMU_VNC_AUTH_SASL_H

#include <sasl/sasl.h>

typedef struct VncStateSASL VncStateSASL;
typedef struct VncDisplaySASL VncDisplaySASL;

#include "authz/base.h"

struct VncStateSASL {
    sasl_conn_t *conn;
    bool wantSSF;
    bool runSSF;
    unsigned int waitWriteSSF;

    const uint8_t *encoded;
    unsigned int encodedLength;
    unsigned int encodedRawLength;
    unsigned int encodedOffset;
    char *username;
    char *mechlist;
};

struct VncDisplaySASL {
    QAuthZ *authz;
    char *authzid;
};

bool vnc_sasl_server_init(Error **errp);
void vnc_sasl_client_cleanup(VncState *vs);

size_t vnc_client_read_sasl(VncState *vs);
size_t vnc_client_write_sasl(VncState *vs);

void start_auth_sasl(VncState *vs);

#endif
