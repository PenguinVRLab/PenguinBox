# Plan — real Steel Battalion controller, zero setup

**Goal, in one sentence:** a user plugs in a real Steel Battalion controller,
launches PenguinBox, and plays — **no Monitor commands, no `device_add`** —
and every feature we have built keeps working (cockpit overlay, VR world
panel, calibration, snapshots).

Written 2026-07-27. This is the **execution** plan. The **research and
architecture** it rests on is [`sbc-input-plan.md`](sbc-input-plan.md)
(adversarially verified 2026-07-25) — this document does not repeat it, it
sequences it and adds the zero-setup UX work that came out of the
2026-07-27 owner discussion.

---

## 1. Why this shape (the decision, restated compactly)

The community currently uses **USB passthrough**, via Monitor:

```
device_add usb-host,vendorid=0x0a7b,productid=0xd000,port=1.3
```

That hands the real device straight to the guest. It works, and it is
**architecturally a dead end for us**: nothing traverses `ui/xemu-input.c`,
so `xemu_input_get_keyboard_sbc()` is NULL and therefore the **F3 cockpit
overlay, the VR SBC world panel, and all calibration/diagnostics are dead by
construction** — not unimplemented, impossible. (Verified in
`sbc-input-plan.md` §1: the passthrough UI PR uses a literal
`static ControllerState fake_state = {0}` and parses no input reports at all.)

**What we build instead:** a host-side libusb reader that reads the real
controller and feeds our **emulated** XID device. The guest keeps talking to
the same emulated device it already trusts; every feature keeps working.
Upstream endorses this shape — PR #2933 (lightgun) does exactly this.

Passthrough stays exactly as it is: **linked in, no UI, no config key**,
documented as an unsupported dev tool (§7 of the research plan).

## 2. Status as of 2026-07-27

| | |
|---|---|
| libusb on the rig (Phase 0.1) | **DONE** — `libusb-1.0-0-dev` installed, `CONFIG_USB_LIBUSB` defined in the rig build. *(Supersedes the research plan's claim that it is `#undef` there.)* |
| `hw/usb/host-libusb.c` (passthrough transport) | present, builds when libusb is found |
| Emulated SBC device | shipped and field-used (keyboard/mouse-fed) |
| Everything else in this plan | **not started** — no merge layer, no reader, no udev rule, no autodetect |

## 3. The one thing we cannot make zero-setup

We can remove every in-emulator step. We **cannot** remove a one-time OS
permission step, and the plan must not pretend otherwise:

- **Linux:** libusb cannot open the device as a normal user without a udev
  rule. We ship none today and packaging installs none. A `.deb` or flatpak
  can install one; **an AppImage structurally cannot** (single file, no
  install step) — so the AppImage path needs a small shipped helper or a
  documented one-liner. The rule needs **both** mechanisms: `TAG+="uaccess"`
  *and* a `MODE`/`GROUP` fallback, because an SSH session has no seat and
  `uaccess` then silently never applies.
- **Windows** (likely the community's platform): libusb cannot detach a
  driver at all; the device needs a one-time admin **Zadig** WinUSB bind.
  Class `0x58` has no in-box driver so nothing is being stolen, but we cannot
  do it from inside the app.

**Framing that is true and worth stating publicly:** this is not a new tax.
Passthrough needs *the same* OS permission **plus** the Monitor commands
**plus** it loses the overlay. We are reducing setup, not adding it.

## 4. Workstreams, in dependency order

Each says whether it needs the physical controller. **Most of this does not.**

> **Build order decision (owner, 2026-07-27): the diagnostics panel goes
> FIRST — W1a below — ahead of the merge layer.** Rationale, in the order it
> matters:
> 1. It attacks the real blocker. The unavoidable friction is the OS
>    permission step, and today that failure is **silent**. A panel that says
>    "controller detected but I cannot open it — here is the exact fix" is the
>    single highest-value thing we can ship the community.
> 2. It de-risks W2's hard parts (open, detach `xpad`, claim, async transfers)
>    **with no guest in the loop** — a failure is a blank panel, not a broken
>    game.
> 3. It turns every hardware owner into a data source for the three fields
>    nobody has ever documented (input byte 7, aiming-lever low bytes, tuner
>    upper nibble). Today those need a usbmon capture, which is a skill
>    barrier; a raw-bytes readout needs only a willing user and a screenshot.
>    That unblocks W1's golden vectors.
>
> **The reframe that makes it safe to build first:** the panel reads **raw
> device reports, not `ControllerState`**. The wire format is fully specified
> and verified; `ControllerState` is exactly what W1 is about to rewrite.
> Read raw and the panel has no dependency on the merge layer.

### W0 — Foundation fixes (no hardware)
Pre-existing defects the new work would inherit or amplify. Two were
independently rediscovered from the code side while writing spec B6.
- **F2 — driver-string write-back.** An unrecognised `portN_driver` resolves
  silently to Duke and is then *written back to config*, destroying an SBC
  selection. Fail loudly; never persist an unrecognised binding.
- **F3 — savestate guard.** The vmstate section id embeds the device type
  name, so a Duke↔SBC change makes existing snapshots fail **mid-load**,
  after RAM/CPU are overwritten — a torn VM. Record the bound driver in the
  snapshot's extra-data blob and refuse the load **up front**.
- **Pedal-scancode OOB.** The SBC sanitize loop covers indices 0–48; the
  three pedal keys live at 49–51, so an out-of-range toml value indexes the
  SDL key array out of bounds every poll (`ui/xemu-input.c:394-410`).
- **F1 — hotkey arbiter** (can trail the rest): one
  `xemu_hotkeys_dispatch()` with an explicit "keyboard is owned by the guest
  device" predicate, replacing today's predicate-gating at five sites.

**Done when:** each has a test or a deliberate reproduction, and F3 refuses a
mismatched snapshot with a clear message instead of tearing the VM.

### W1a — The device diagnostics panel (FIRST; hardware to exercise, not to build)

A read-only "show me, live, what the emulator thinks my hardware is doing"
screen. Design already written: `sbc-analog-input.md` §7 (what it answers,
what it must show), §10 (the pre-flight checklist that gates launch). This
workstream **builds that panel's read-only half** and, underneath it, the
device layer W2 will later take over.

**Scope IN:**
- **Device layer** (the reusable part): enumerate `0x0A7B:0xD000`, open,
  auto-detach, claim, read EP `0x82`. This *is* W2's device layer — built
  once, consumed read-only for now.
- **Live readout:** all 44 SBC functions, **raw value and decoded value side
  by side**, plus source attribution. Raw is what distinguishes "the device
  is broken" from "our decode is wrong".
- **Fault hints** where unambiguous: axis pinned at max, axis never reaching
  full travel, input never firing, axis jittering at rest.
- **The permission story, made legible** — the actual point of going first:
  *detected and working* / *detected but no permission (install the udev
  rule: …)* / *detected but not WinUSB-bound (run Zadig)* / *not detected*.
- **Report export** — dump the raw report stream to a file. This is what
  turns a community member with hardware into the source of W1's golden
  vectors, without asking them to learn usbmon.

**Scope OUT of this slice** (deliberately — it is a lot of state machinery
and none of it is needed to show gauges): calibration persistence, device
fingerprinting, per-game profiles, validity invalidation
(`sbc-analog-input.md` §8/§9). Those land on top once the device layer is
proven.

**Design constraints to honor from day one:**
- **Exactly one owner of the device handle.** When W2's reader arrives it
  *takes over* this device layer; a second opener would collide on the
  libusb claim.
- **Do not inherit the F3 overlay's UI recipe.** That overlay is
  deliberately `NoInputs`/click-through (spec A4); a config panel must accept
  clicks. A4 lists this boundary as undecided — this workstream decides it.
- **`test_mode` freezes the last guest report** (spec B6 I14): a button held
  when the panel opens stays held from the guest's view — materially bad
  mid-ignition. Needs a neutral-report path. Extend
  `xemu_input_get_test_mode()` (`hw/xbox/xid.c:57`), the existing in-tree
  pattern for "read and display without feeding the game", rather than
  inventing one.

**Done when:** a user with a real controller can open the panel, see every
control respond, be told *specifically* what to do if it cannot be opened,
and export a report capture. No guest involvement required.

### W1b — Sim registry + the pre-flight gate (no hardware)

**Owner requirement, restated 2026-07-27:** *"the tool should open when you
attempt to boot Steel Battalion, Line of Contact, or any other game we deem a
'sim' that requires controller profiles."* This is the pre-flight checklist
designed in `sbc-analog-input.md` §10 — the flow, the verdict levels
(ready/warning/blocking), and the always-available Skip are specified there
and are not restated here. What follows is the part §10 left open.

**Title IDs — measured 2026-07-27** by parsing the XBE cert out of the retail
discs (never recorded before this):

| Title | XBE title ID |
|---|---|
| Steel Battalion (USA) | `0x43430002` (confirms the constant in `xbox-game-hacks.c`) |
| Steel Battalion: Line of Contact | `0x43430009` |

Both are Capcom (`0x4343`). The registry is keyed on title ID, **not** name or
hash — same key the profile system (§8) uses.

**HOOK POINT — decision (§10 recorded this as "Implementation decision
pending"): probe guest memory just after the XBE is mapped, then pause.**

The alternative — read `default.xbe` off the disc before boot — is **not
available**: there is no XDVDFS/XISO parser in the tree (verified), and
`xemu_get_xbe_info()` reads the header from **guest memory at 0x10000**
(`xemu-xbe.c:82-96`), so it cannot answer before the guest has loaded the
title. Building a disc parser to move the gate earlier is not worth it,
because:

- The XBE header is mapped by the loader **very early** — before the game's
  entry point runs, and therefore **before the game enumerates controllers**.
  That is the property that actually matters (F4: SB is believed to enumerate
  once at boot), because it means a controller fixed at the checklist can
  still be seen by the game.
- With the VM paused at that moment, the user-visible behavior is exactly the
  requirement: attempt to boot a sim → the tool opens → fix or Skip → the
  game continues.

**Mechanism:** reuse the probe already proven in `xbox-game-hacks.c` —
XBE header magic at `0x10000`, cert VA at `+0x118`, title ID at `+0x8`
(constants `XBE_HDR_VADDR` / `XBE_HDR_CERT_OFF` / `XBE_CERT_TITLEID_OFF`).
Poll it on a timer from machine start; on the first successful read, look up
the registry and either `vm_stop()` + open the checklist, or do nothing at
all and never poll again.

**Invariants this must hold:**
1. **Non-sim titles pay nothing.** The probe stops permanently after the
   first successful title read. No gate, no modal, no polling.
2. **Skip is a deliberate press, never the default** (§10). A user who wants
   to play with a broken pedal is allowed to.
3. **Re-openable during play** (§10) — the Machine-menu entry from W1a stays;
   the gate is the same panel in a gating role, not a second window.
4. **Never blocks boot when the feature is off.** With the host reader
   disabled or libusb absent, the registry may still recognise a sim, but the
   checklist must degrade to "no device layer — nothing to check" rather than
   standing between the user and their game.

**Profile schema:** §10 requires each control be marked `required` vs
`optional`. Minimum viable registry entry: title ID, display name, and the
required-device list. Full per-game profiles (§8) land later — this
workstream needs only enough schema to answer "is anything blocking?".

### W1 — The merge layer (no hardware)
The core new work, in **new files** so the diff stays rebasable against
upstream PR #1803. Every source publishes a complete, immutable frame of
**absolute** values; a **pure function** merges frames inside the refresh.
Per-control policy is explicit: momentary buttons OR; toggles/gear/tuner
last-writer-wins with a defined owner (**a real controller owns these
absolutely** — otherwise its physical switch level and our XOR model desync
permanently); axes by priority or largest deviation from centre. Drop
`previousButtons`/XOR entirely.

**Done when:** unit tests pass **off-rig** against a synthetic replay source,
including a recorded report stream replayed byte-for-byte. This is the piece
that makes everything else testable before hardware exists.

### W2 — The host-side libusb reader (hardware for final validation only)
Behind a flag from day one (§5). Key constraints, all already researched:
own libusb context; `libusb_set_auto_detach_kernel_driver` **not** the
explicit-detach pattern `host-libusb.c` uses (survives SIGKILL); 2–4 async
transfers in flight, resubmitted from the callback; **no new thread** —
integrate libusb pollfds into the `qemu_main` loop so callbacks run under the
same BQL as `update_sbc_input()`; bypass `xemu_input_update_controller()` and
its 2500 µs limiter entirely; suppress the phantom `xpad` joystick.

**Developable against W1's replay source; needs the real device to finish.**

### W3 — Zero-setup UX (mostly no hardware)
- **Autodetect**, mirroring the existing SDL pattern
  (`ui/xemu-input.c:468,533`): hotplug callback for `0x0A7B:0xD000` → bind as
  a frame source → configure the port for the emulated SBC.
- **Session-scoped, never persisted.** Autodetect must **not** write the
  driver back to config — that is F2's corruption path with a new trigger.
- **Legible failures, specific not generic:** "controller detected but no
  permission — install the udev rule"; "detected but not WinUSB-bound — run
  Zadig"; "connected". Silent failure is what started this whole thread.
- **Timing:** the device must be decided **before the guest boots** (see W5).

### W4 — Platform enablement (no hardware to write; hardware to verify)
Write our own udev rule (the commonly-copied `libSteelBattalion` one is
**verified broken** — see research plan §Reusable prior art). Ship it in the
deb/flatpak; provide a helper or documented one-liner for the AppImage.
Windows: thin platform interface + the specific not-WinUSB-bound error.
**Coordinate with whoever owns packaging** — this is a packaging deliverable,
adjacent to the libusb `--enable-libusb` work (tooling-gaps G6).

### W5 — Hot-swap: test, then decide (Test B needs no hardware)
Specified in [`sbc-input-plan.md`](sbc-input-plan.md) §9 item 6. The key
distinction: **device-level** swap (qdev replug — guest re-enumerates,
expected to fail per F4) vs **source-level** swap (emulated device stays
plugged, only the feeding source changes — guest cannot tell).

If source-level passes and device-level fails, the product rule is:
**decide the device before boot, let the source float freely afterwards** —
which *is* the plug-it-in-whenever experience. Implied design rule to adopt:
**always instantiate the emulated SBC when a port is configured for it**,
whether or not real hardware is attached.

### W6 — Prove the features actually survive (hardware)
The entire justification for this architecture is that the overlay, VR panel
and calibration keep working with a real controller. Verify each explicitly
rather than assuming: F3 overlay shows live real-hardware state; the VR world
panel composites in-headset; a snapshot saves and loads with the reader
active. Also the backlog win passthrough can't reach without our own work:
lighting the **real** cockpit LEDs from the game's OUT report.

## 5. The flag contract (development gate)

Standard lever doctrine (spec A2): `XEMU_SBC_HOST_READER=1` to enable,
`XEMU_NO_SBC_HOST_READER` as kill switch (kill wins), an arming banner on
stderr so scripts can prove it was on, **default OFF**, and byte-identical
behavior when off.

**The flag is for development, not the destination.** Shipping target is
default-on with the kill switch retained — a user who has to set an
environment variable has not had setup removed.

## 6. Hardware dependency

Only three things genuinely require a physical controller: **capturing the
report stream** to pin down the three fields nobody has ever dumped (input
byte 7, the aiming-lever low bytes, the tuner's upper nibble), **W2's final
validation**, and **W6**. Everything else — W0, W1a's code, W1, W3, W4, W5's
Test B — is buildable and testable without one.

**W1a changes who can supply that capture.** The research plan assumed a
usbmon capture, which needs Linux plus tooling skill. W1a's export makes the
emulator itself the capture instrument, so **any community member with a
controller can produce it** — a materially wider pool than "someone who can
drive usbmon". Grab it as early as a controller is available: it is the input
every later golden-vector test is written against.

## 7. Definition of done

A user on a supported platform, having done the one-time OS permission step:
plugs in the controller, launches PenguinBox, and plays Steel Battalion with
the cockpit overlay and (in VR) the world panel live — having typed nothing
into a Monitor and changed nothing in a menu. Unplugging and replugging
mid-session does not interrupt play. The old passthrough recipe still works
for anyone who wants it, documented as unsupported.

## 8. Explicitly not doing

- **Passthrough as a user-facing feature** (research plan §7) — keep the
  transport as-is, document the recipe as an unsupported dev tool with its
  two real gotchas: player 1 must be unbound or port `1.3` collides
  (`hw/usb/bus.c:406`), and a build without libusb rejects `usb-host`
  outright.
- **Upstream PR #2854** (passthrough UI) — duplicates the binding path and
  conflicts with PR #1803 on 7 of 13 files.
- **Passthrough as a fidelity oracle** — contaminated by our own OHCI
  one-active-packet limitation (xemu issue #1239, open since 2022, filed
  against a real Steel Battalion controller). Use a usbmon capture instead.
