/*
 * Input Visitor
 *
 * Copyright (C) 2017 Red Hat, Inc.
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QOBJECT_INPUT_VISITOR_H
#define QOBJECT_INPUT_VISITOR_H

#include "qapi/visitor.h"

typedef struct QObjectInputVisitor QObjectInputVisitor;

Visitor *qobject_input_visitor_new(QObject *obj);

Visitor *qobject_input_visitor_new_keyval(QObject *obj);

Visitor *qobject_input_visitor_new_str(const char *str,
                                       const char *implied_key,
                                       Error **errp);

#endif
