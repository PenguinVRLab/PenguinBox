# Quickstart

## What you need

- An Xbox **MCPX boot ROM** and a **BIOS/flash image**
- An **EEPROM** image and a formatted **hard-disk image**
- Your own game media

None of these are distributed here.

## Windows

1. Download `PenguinBox-win64.exe` from the release.
2. Run it. It is self-contained — no installer, no runtime to fetch.
3. On first launch, point it at your BIOS, MCPX, EEPROM and HDD image.
4. Load a disc image and boot.

## Linux

1. Download `PenguinBox-x86_64.AppImage`.
2. `chmod +x PenguinBox-x86_64.AppImage && ./PenguinBox-x86_64.AppImage`
3. Same first-launch setup as above.

The AppImage bundles its libraries but uses the host's glibc, so it needs a
reasonably current distribution. If it refuses to start with
`version 'GLIBC_2.39' not found`, that is why — build from source, or use a
newer distro.

## Confirming the performance paths engaged

Launch from a terminal and look for these lines. They are the emulator telling
you what it decided:

```
perf-title: 0x43430009 — measured performance paths ENABLED (validated title)
present-dedup: skip recomposite of an unchanged display surface ON
nv2a: surf2tex-ext ON (validated title)
```

A title id of `0x43430002` is Steel Battalion and `0x43430009` is Line of
Contact. Any other id will report that it is staying on stock defaults — that is
intended.

## The Steel Battalion controller

**Real controller (USB passthrough).** Plug it in before launching and select it
in the emulator's USB passthrough settings. On Linux your user needs permission
on the device node — a udev rule is the usual fix; without it the controller is
visible but cannot be claimed.

**Without the controller.** An emulated SBC is driven from the keyboard. Press
**F3** to toggle the cockpit overlay, which shows which panel switches are
currently set.

## Reporting a problem

Start PenguinBox from a terminal and save what it prints:

```
./PenguinBox-x86_64.AppImage 2>&1 | tee penguinbox.log
```

The log starts with the build's version and prints the state of the renderer's
switches. Send it with a description of what you saw and roughly when it happened.

## If something renders wrong

Each performance path can be switched off individually, on the same build, so
you can find which one is responsible without rebuilding:

| variable | disables |
|---|---|
| `XEMU_NO_SURF2TEX_FEEDBACK=1` | sampling a render target while it is still bound |
| `XEMU_NO_SURF2TEX_ZEROCOPY=1` | borrowing a surface image instead of copying it |
| `XEMU_NO_ALIAS_SURFACES=1` | keeping large overlapped surfaces resident |
| `XEMU_NO_SURF2TEX_EXT=1` | the multi-surface gather copy |
| `XEMU_NO_PRESENT_DEDUP=1` | display composite de-duplication |

Setting all five gives you stock rendering for comparison. **If one of them fixes
what you are seeing, that single line is the most useful thing you can tell us.**

## Trying the paths on another game

They are off by default outside the two Steel Battalion titles. To try them
anyway:

```
XEMU_SURF2TEX_EXT=1 XEMU_ALIAS_SURFACES=1 XEMU_SURF2TEX_ZEROCOPY=1 \
XEMU_SURF2TEX_FEEDBACK=1 XEMU_PRESENT_DEDUP=1
```

Results either way are worth hearing about — it is how a title gets added to the
validated list.
