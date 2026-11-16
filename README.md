# PenguinBox

An original-Xbox emulator focused on **Steel Battalion** and **Steel Battalion:
Line of Contact** — a fork of [xemu](https://xemu.app), which is itself a fork
of [QEMU](https://www.qemu.org).

This build exists for one reason: to make those two titles run well, including
with a real Steel Battalion controller over USB passthrough.

## What is different from upstream xemu

**Per-title performance paths.** Steel Battalion and Line of Contact trigger a
surface-readback pattern that costs hundreds of blocking GPU→CPU round trips
per frame in a stock build. This fork adds GPU-side paths that avoid them —
a multi-surface gather copy, surface aliasing, zero-copy surface-as-texture
binds, and feedback-tolerant sampling — plus de-duplication of redundant
display composites.

Those paths **enable themselves automatically for those two titles** and leave
every other game on stock behaviour, because those are the only titles they
have been measured and validated against.

**Steel Battalion controller support.** An emulated SBC device with a keyboard
mapping, and a cockpit overlay showing panel state. USB passthrough for a real
controller is compiled in and bundled.

## Status

**Experimental.** This is a pre-release aimed at people who play these games and
can tell us when something looks wrong. It is not a general-purpose xemu
replacement — for anything other than Steel Battalion, use upstream xemu.

## Running it

See [QUICKSTART.md](QUICKSTART.md). Known problems and how to report them are in
[KNOWN-ISSUES.md](KNOWN-ISSUES.md).

You supply your own BIOS/MCPX images and game media. None are included, and none
will be.

## Licence

The aggregate is **GPL v2** (inherited from QEMU). Individual files carry their
own headers — see `LICENSE`, `COPYING`, and the SPDX identifiers in each file.
The VR and Steel Battalion code written specifically for this fork is MIT, which
flows cleanly into the GPLv2 aggregate and stays reusable.

## Credit

The overwhelming majority of this software is other people's work: QEMU, the
xemu project, and the contributors who made original-Xbox emulation viable at
all. The techniques here are standard ones applied carefully to two specific
games — nothing about the underlying emulation was invented here.
