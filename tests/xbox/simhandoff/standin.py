#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Patrick Carey
# SPDX-License-Identifier: MIT
"""A stand-in Sim Configurator for tests/xbox/simhandoff (the design's A3, restated): it takes the hand-off's
command line, records it, and ends as STANDIN_MODE says. Its result is written the way the app writes it
(sim-configurator src/sim_configurator/handoff.py at 1764901: `yaml.safe_dump(doc, sort_keys=False)` of
result_document's keys, atomically), so the reader is held to the app's real output form. Not a tool: the test's
fixture.

STANDIN_MODE: saved | unchanged | cancel | busy | usage | crash | nofile | badid | wrongtitle | disagree | slow
             (a signal: Cancel boot, as the app) | stubborn (ignores SIGTERM)
STANDIN_MARK: a file the stand-in writes its arguments to (the test reads it; absent = never started)
STANDIN_READY: a file the slow and stubborn modes create once their signal handlers are installed
"""
import os
import signal
import sys
import time

import yaml


def main(argv):
    mark = os.environ.get("STANDIN_MARK")
    if mark:
        with open(mark, "w", encoding="utf-8") as fh:
            fh.write("\n".join(argv) + "\n")
    mode = os.environ.get("STANDIN_MODE", "saved")
    args = dict(zip(argv[0::2], argv[1::2]))
    if mode == "usage" or "--result" not in args or "--request-id" not in args:
        return 2
    # the app's contract (sim-configurator 4d6ce17, docs/profile-schema.md 9): a missing result folder or profiles
    # folder is a usage error
    if not os.path.isdir(os.path.dirname(args["--result"])) or not os.path.isdir(args.get("--profiles", "")):
        return 2
    if mode == "busy":
        return 4
    if mode == "crash":
        os.kill(os.getpid(), signal.SIGSEGV)
    if mode == "stubborn":   # an app that ignores the signal (or an old one): PenguinBox kills it after 3 s
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        ready()
        time.sleep(60)
        return 0
    if mode == "slow":
        # the owner, 2026-11-15, "write it as a cancel boot": SIGTERM, SIGHUP, SIGINT end the session as Cancel boot,
        # the result written with outcome cancel_boot and the request id, then exit 3 (the app's docs/profile-schema.md 9)
        def on_signal(signum, frame):
            write_result(args, "cancel")
            os._exit(3)
        for sig in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
            signal.signal(sig, on_signal)
        ready()
        time.sleep(60)
        return 0
    if mode != "nofile":
        write_result(args, mode)
    return 3 if mode in ("cancel", "disagree") else 0


def ready():
    """Tell the test the signal handlers are installed (STANDIN_READY: a file to create), so its Cancel never
    races this process's start on a loaded machine."""
    path = os.environ.get("STANDIN_READY")
    if path:
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("ready\n")


def write_result(args, mode):
    tid = int(args["--configure-title"], 16)
    outcome = {"saved": "saved", "unchanged": "unchanged", "cancel": "cancel_boot", "disagree": "saved"}.get(mode, "saved")
    doc = {"schema": "sim-result/1",
           "request_id": "pb-someone-else" if mode == "badid" else args["--request-id"],
           "title_id": tid + 1 if mode == "wrongtitle" else tid,
           "outcome": outcome,
           "profiles": args["--profiles"],
           "ports": None if mode == "cancel" else "ports",
           "written": ["profile_sbc_real.yaml", "bindings_sbc_wire.yaml"] if mode == "saved" else [],
           "preflight": "READY"}
    tmp = args["--result"] + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        fh.write(yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
    os.replace(tmp, args["--result"])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
