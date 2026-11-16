/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#ifndef XEMU_SIM_HANDOFF_H
#define XEMU_SIM_HANDOFF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SimHandoffOutcome {
    SIM_HANDOFF_SAVED,
    SIM_HANDOFF_UNCHANGED,
    SIM_HANDOFF_CANCEL_BOOT,
    SIM_HANDOFF_BUSY,
    SIM_HANDOFF_TOO_OLD,
    SIM_HANDOFF_NOT_FOUND,
    SIM_HANDOFF_CRASH,
    SIM_HANDOFF_SKIPPED,
} SimHandoffOutcome;

typedef struct SimHandoffResult {
    SimHandoffOutcome outcome;
    int exit_status;
    char why[320];
    char request_id[48];
    char ports[96];
    char preflight[16];
    int nwritten;
} SimHandoffResult;

const char *xemu_sim_handoff_outcome_name(SimHandoffOutcome o);

char **xemu_sim_handoff_command(const char *configured, char *why, size_t whylen);
void xemu_sim_handoff_strv_free(char **v);
char **xemu_sim_handoff_split(const char *cmd);

char *xemu_sim_handoff_store_dir(const char *leaf);
char *xemu_sim_handoff_profile_dir(void);

SimHandoffOutcome xemu_sim_handoff_judge(bool exited, int exit_status, const char *result_path,
                                         const char *request_id, uint32_t title_id,
                                         SimHandoffResult *out);

bool xemu_sim_handoff_start(uint32_t title_id, const char *title_name, const char *configured_cmd,
                            SimHandoffResult *out);
bool xemu_sim_handoff_active(void);
bool xemu_sim_handoff_poll(SimHandoffResult *out);
void xemu_sim_handoff_cancel(void);

SimHandoffOutcome xemu_sim_handoff_run(uint32_t title_id, const char *title_name,
                                       const char *configured_cmd,
                                       SimHandoffResult *out);

#ifdef __cplusplus
}
#endif

#endif
