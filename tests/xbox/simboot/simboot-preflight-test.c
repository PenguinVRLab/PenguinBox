/*
 * PenguinBox — the Sim Configurator's pre-flight verdict on a saved or unchanged setup (ui/xemu-sim-boot.h:
 * sim_boot_preflight_refuses), alone and after the hand-off's own judge read a result file
 *
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 *
 * The rule (the owner, 2026-11-15, "PenguinBox refuses to boot a setup marked blocked"): BLOCK refuses; READY and WARN
 * boot; an absent or empty value boots as before; any other value refuses. Each result file is written in the form
 * the app writes it (yaml.safe_dump of its keys) and judged by xemu_sim_handoff_judge as a finished run would be.
 * usage: simboot-preflight-test WORK_DIR
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "xemu-sim-boot.h"
#include "xemu-sim-handoff.h"

static int nchecks, nfailed;

static void expect(const char *what, int ok)
{
    nchecks++;
    if (!ok) {
        nfailed++;
    }
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

#define TID 0x43430002u

static void write_result(const char *path, const char *outcome, const char *preflight)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        exit(2);
    }
    fprintf(f, "schema: sim-result/1\nrequest_id: pb-preflight\ntitle_id: %u\noutcome: %s\nprofiles: /p\n"
            "ports: simap_ok\nwritten: []\n", TID, outcome);
    if (preflight) {
        fprintf(f, "preflight: %s\n", preflight);
    }
    fclose(f);
}

static int judged(const char *work, const char *outcome, const char *preflight, char *why, size_t whylen)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/result.yaml", work);
    write_result(path, outcome, preflight);
    SimHandoffResult r;
    SimHandoffOutcome o = xemu_sim_handoff_judge(true, 0, path, "pb-preflight", TID, &r);
    printf("     outcome %s, preflight %s -> %s, preflight read '%s'\n", outcome, preflight ? preflight : "(absent)",
           xemu_sim_handoff_outcome_name(o), r.preflight);
    if (o != SIM_HANDOFF_SAVED && o != SIM_HANDOFF_UNCHANGED) {
        return -1;
    }
    why[0] = '\0';
    return sim_boot_preflight_refuses(r.preflight, why, whylen) ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: simboot-preflight-test WORK_DIR\n");
        return 2;
    }
    const char *work = argv[1];
    mkdir(work, 0755);
    char why[400];
    printf("== F. the app's pre-flight verdict\n");

    expect("F1 saved + BLOCK: refused, not booted", judged(work, "saved", "BLOCK", why, sizeof(why)) == 1);
    printf("     why: %s\n", why);
    expect("F1 ... the reason names the pre-flight check and what to do",
           strstr(why, "pre-flight check blocks this setup") && strstr(why, "Pre-flight page"));
    expect("F2 unchanged + BLOCK: refused, not booted", judged(work, "unchanged", "BLOCK", why, sizeof(why)) == 1);
    expect("F3 saved + READY: booted as before", judged(work, "saved", "READY", why, sizeof(why)) == 0);
    expect("F4 unchanged + WARN: booted as before", judged(work, "unchanged", "WARN", why, sizeof(why)) == 0);
    expect("F5 saved + WARN: booted as before", judged(work, "saved", "WARN", why, sizeof(why)) == 0);
    expect("F6 saved, the field absent: booted as before", judged(work, "saved", NULL, why, sizeof(why)) == 0);
    expect("F7 unchanged, the field empty (''): booted as before", judged(work, "unchanged", "''", why, sizeof(why)) == 0);
    expect("F8 saved + an unknown value (MAYBE): refused, the safer reading",
           judged(work, "saved", "MAYBE", why, sizeof(why)) == 1 && strstr(why, "does not pass"));
    expect("F9 saved + block in lower case: an unknown value, refused", judged(work, "saved", "block", why, sizeof(why)) == 1);
    expect("F10 cancel_boot + BLOCK (exit 0): not a saved or unchanged end, never reaches the check",
           judged(work, "cancel_boot", "BLOCK", why, sizeof(why)) == -1);

    expect("F11 NULL and \"\": not refused; WHY untouched by a pass",
           !sim_boot_preflight_refuses(NULL, NULL, 0) && !sim_boot_preflight_refuses("", why, sizeof(why)));
    expect("F12 BLOCK with no WHY buffer: refused, no write", sim_boot_preflight_refuses("BLOCK", NULL, 0));

    printf("%d checks, %d failed\n", nchecks, nfailed);
    return nfailed ? 1 : 0;
}
