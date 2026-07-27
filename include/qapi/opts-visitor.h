/*
 * Options Visitor
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Author: Laszlo Ersek <lersek@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef OPTS_VISITOR_H
#define OPTS_VISITOR_H

#include "qapi/visitor.h"

#define OPTS_VISITOR_RANGE_MAX 65536

typedef struct OptsVisitor OptsVisitor;

Visitor *opts_visitor_new(const QemuOpts *opts);

#endif
