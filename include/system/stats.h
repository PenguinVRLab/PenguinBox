/*
 * Copyright (c) 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef STATS_H
#define STATS_H

#include "qapi/qapi-types-stats.h"

typedef void StatRetrieveFunc(StatsResultList **result, StatsTarget target,
                              strList *names, strList *targets, Error **errp);
typedef void SchemaRetrieveFunc(StatsSchemaList **result, Error **errp);

void add_stats_callbacks(StatsProvider provider,
                         StatRetrieveFunc *stats_fn,
                         SchemaRetrieveFunc *schemas_fn);

void add_stats_entry(StatsResultList **, StatsProvider, const char *id,
                     StatsList *stats_list);
void add_stats_schema(StatsSchemaList **, StatsProvider, StatsTarget,
                      StatsSchemaValueList *);

bool apply_str_list_filter(const char *string, strList *list);

#endif
