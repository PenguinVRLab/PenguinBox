/*
 * Output Visitor
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QOBJECT_OUTPUT_VISITOR_H
#define QOBJECT_OUTPUT_VISITOR_H

#include "qapi/visitor.h"

typedef struct QObjectOutputVisitor QObjectOutputVisitor;

Visitor *qobject_output_visitor_new(QObject **result);

#endif
