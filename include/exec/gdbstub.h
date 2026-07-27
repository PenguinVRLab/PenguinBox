#ifndef GDBSTUB_H
#define GDBSTUB_H

typedef struct GDBFeature {
    const char *xmlname;
    const char *xml;
    const char *name;
    const char * const *regs;
    int num_regs;
} GDBFeature;

typedef struct GDBFeatureBuilder {
    GDBFeature *feature;
    GPtrArray *xml;
    GPtrArray *regs;
    int base_reg;
} GDBFeatureBuilder;


typedef int (*gdb_get_reg_cb)(CPUState *cpu, GByteArray *buf, int reg);
typedef int (*gdb_set_reg_cb)(CPUState *cpu, uint8_t *buf, int reg);

void gdb_init_cpu(CPUState *cpu);

void gdb_register_coprocessor(CPUState *cpu,
                              gdb_get_reg_cb get_reg, gdb_set_reg_cb set_reg,
                              const GDBFeature *feature, int g_pos);

void gdb_unregister_coprocessor_all(CPUState *cpu);

bool gdbserver_start(const char *port_or_device, Error **errp);

void gdb_feature_builder_init(GDBFeatureBuilder *builder, GDBFeature *feature,
                              const char *name, const char *xmlname,
                              int base_reg);

void G_GNUC_PRINTF(2, 3)
gdb_feature_builder_append_tag(const GDBFeatureBuilder *builder,
                               const char *format, ...);

void gdb_feature_builder_append_reg(const GDBFeatureBuilder *builder,
                                    const char *name,
                                    int bitsize,
                                    int regnum,
                                    const char *type,
                                    const char *group);

void gdb_feature_builder_end(const GDBFeatureBuilder *builder);

const GDBFeature *gdb_find_static_feature(const char *xmlname);

int gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);

int gdb_write_register(CPUState *cpu, uint8_t *mem_buf, int reg);

typedef struct {
    int gdb_reg;
    const char *name;
    const char *feature_name;
} GDBRegDesc;

GArray *gdb_get_register_list(CPUState *cpu);

void gdb_set_stop_cpu(CPUState *cpu);

extern const GDBFeature gdb_static_features[];

#endif
