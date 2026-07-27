#ifndef GDBSTUB_COMMANDS_H
#define GDBSTUB_COMMANDS_H

typedef void (*GdbCmdHandler)(GArray *params, void *user_ctx);

typedef enum GDBThreadIdKind {
    GDB_ONE_THREAD = 0,
    GDB_ALL_THREADS,
    GDB_ALL_PROCESSES,
    GDB_READ_THREAD_ERR
} GDBThreadIdKind;

typedef union GdbCmdVariant {
    const char *data;
    uint8_t opcode;
    unsigned long val_ul;
    unsigned long long val_ull;
    struct {
        GDBThreadIdKind kind;
        uint32_t pid;
        uint32_t tid;
    } thread_id;
} GdbCmdVariant;

#define gdb_get_cmd_param(p, i)    (&g_array_index(p, GdbCmdVariant, i))

typedef struct GdbCmdParseEntry {
    GdbCmdHandler handler;
    const char *cmd;
    bool cmd_startswith;
    const char *schema;
    bool allow_stop_reply;
    bool need_cpu_context;
} GdbCmdParseEntry;

int gdb_put_packet(const char *buf);

void gdb_extend_query_table(GPtrArray *table);

void gdb_extend_set_table(GPtrArray *table);

void gdb_extend_qsupported_features(char *qsupported_features);

void gdb_hextomem(GByteArray *mem, const char *buf, int len);

#endif
