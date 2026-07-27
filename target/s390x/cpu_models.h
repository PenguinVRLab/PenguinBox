/*
 * CPU models for s390x
 *
 * Copyright 2016 IBM Corp.
 *
 * Author(s): David Hildenbrand <dahi@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef TARGET_S390X_CPU_MODELS_H
#define TARGET_S390X_CPU_MODELS_H

#include "cpu_features.h"
#include "target/s390x/gen-features.h"
#include "hw/core/cpu.h"

typedef struct S390CPUDef {
    const char *name;
    const char *desc;
    uint8_t gen;
    uint16_t type;
    uint8_t ec_ga;
    uint8_t mha_pow;
    uint32_t hmfai;
    S390FeatBitmap base_feat;
    S390FeatInit base_init;
    S390FeatBitmap default_feat;
    S390FeatInit default_init;
    S390FeatBitmap full_feat;
    S390FeatInit full_init;
} S390CPUDef;

typedef struct S390CPUModel {
    const S390CPUDef *def;
    S390FeatBitmap features;
    /* values copied from the "host" model, can change during migration */
    uint16_t lowest_ibc;
    uint32_t cpu_id;
    uint8_t cpu_id_format;
    uint8_t cpu_ver;
} S390CPUModel;

#define cpuid_type(x)     (((x) >> 16) & 0xffff)
#define cpuid_id(x)       (((x) >> 32) & 0xffffff)
#define cpuid_ver(x)      (((x) >> 56) & 0xff)
#define cpuid_format(x)   (((x) >> 15) & 0x1)

#define lowest_ibc(x)     (((uint32_t)(x) >> 16) & 0xfff)
#define unblocked_ibc(x)  ((uint32_t)(x) & 0xfff)
#define has_ibc(x)        (lowest_ibc(x) != 0)

#define S390_GEN_Z10 0xa
#define ibc_gen(x)        (x == 0 ? 0 : ((x >> 4) + S390_GEN_Z10))
#define ibc_ec_ga(x)      (x & 0xf)

void s390_cpudef_featoff(uint8_t gen, uint8_t ec_ga, S390Feat feat);
void s390_cpudef_featoff_greater(uint8_t gen, uint8_t ec_ga, S390Feat feat);
void s390_cpudef_group_featoff_greater(uint8_t gen, uint8_t ec_ga,
                                       S390FeatGroup group);
uint32_t s390_get_hmfai(void);
uint8_t s390_get_mha_pow(void);
uint32_t s390_get_ibc_val(void);
static inline uint16_t s390_ibc_from_cpu_model(const S390CPUModel *model)
{
    uint16_t ibc = 0;

    if (model->def->gen >= S390_GEN_Z10) {
        ibc = ((model->def->gen - S390_GEN_Z10) << 4) + model->def->ec_ga;
    }
    return ibc;
}
void s390_get_feat_block(S390FeatType type, uint8_t *data);
bool s390_has_feat(S390Feat feat);
uint8_t s390_get_gen_for_cpu_type(uint16_t type);
static inline bool s390_known_cpu_type(uint16_t type)
{
    return s390_get_gen_for_cpu_type(type) != 0;
}
#define CPU_ID_SHIFT 32
#define CPU_ID_BITS 24
#define CPU_PHYS_ADDR_BITS 4
#define CPU_PHYS_ADDR_SHIFT (CPU_ID_SHIFT + CPU_ID_BITS - CPU_PHYS_ADDR_BITS)
static inline uint64_t s390_cpuid_from_cpu_model(const S390CPUModel *model)
{
    return ((uint64_t)model->cpu_ver << 56) |
           ((uint64_t)model->cpu_id << CPU_ID_SHIFT) |
           ((uint64_t)model->def->type << 16) |
           (model->def->gen == 7 ? 0 : (uint64_t)model->cpu_id_format << 15);
}
S390CPUDef const *s390_find_cpu_def(uint16_t type, uint8_t gen, uint8_t ec_ga,
                                    S390FeatBitmap features);
void s390_set_qemu_cpu_model(uint16_t type, uint8_t gen, uint8_t ec_ga,
                             const S390FeatInit feat_init);
void s390_cpu_list(void);

bool kvm_s390_cpu_models_supported(void);
bool kvm_s390_get_host_cpu_model(S390CPUModel *model, Error **errp);
bool kvm_s390_apply_cpu_model(const S390CPUModel *model,  Error **errp);

#endif
