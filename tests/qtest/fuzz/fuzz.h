/*
 * fuzzing driver
 *
 * Copyright Red Hat Inc., 2019
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QTEST_FUZZ_H
#define QTEST_FUZZ_H

#include "qemu/units.h"
#include "qapi/error.h"

#include "tests/qtest/libqtest.h"

typedef struct FuzzTarget {
    const char *name;
    const char *description;


    GString *(*get_init_cmdline)(struct FuzzTarget *);

    void(*pre_vm_init)(void);

    void(*pre_fuzz)(QTestState *);

    void(*fuzz)(QTestState *, const unsigned char *, size_t);

    size_t(*crossover)(const uint8_t *data1, size_t size1,
                       const uint8_t *data2, size_t size2,
                       uint8_t *out, size_t max_out_size,
                       unsigned int seed);

    void *opaque;
} FuzzTarget;

void flush_events(QTestState *);
void fuzz_reset(QTestState *);

void fuzz_qtest_set_serialize(bool option);

void fuzz_add_target(const FuzzTarget *target);

size_t LLVMFuzzerCustomCrossOver(const uint8_t *data1, size_t size1,
                                 const uint8_t *data2, size_t size2,
                                 uint8_t *out, size_t max_out_size,
                                 unsigned int seed);
int LLVMFuzzerTestOneInput(const unsigned char *Data, size_t Size);
int LLVMFuzzerInitialize(int *argc, char ***argv, char ***envp);

#endif
