/*
 * Forwarding visitor
 *
 * Copyright Red Hat, Inc. 2021
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef FORWARD_VISITOR_H
#define FORWARD_VISITOR_H

#include "qapi/visitor.h"

typedef struct ForwardFieldVisitor ForwardFieldVisitor;

Visitor *visitor_forward_field(Visitor *target, const char *from, const char *to);

#endif
