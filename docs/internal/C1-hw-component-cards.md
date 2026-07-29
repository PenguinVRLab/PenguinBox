# C1 — Tier-C component cards: inherited Xbox hardware emulation

> **PRIVATE** — lives under `docs/internal/`, never merged to the public repo (owner rule, see [README](README.md)).

**What these are.** One-page cards (per [00-specification-plan.md](00-specification-plan.md) §1, Tier C) for
inherited xemu hardware components we *operate but rarely touch*. Each card: Purpose / Interfaces /
Invariants-we-rely-on / Danger notes / Escalation trigger. They are situational awareness, not behavioral
contracts — deep enough to know what a component is, where its edges are, and when to stop trusting the card.

**The escalation rule (non-negotiable):** *the moment a Tier-C component gets modified, its card must be
upgraded to a full Tier-B as-built spec as part of that same change.* Each card names the change-types that trip this.

**Tag legend:** `[VERIFIED]` = backed by cited experiment/log/in-tree assert · `[DERIVED]` = read from code,
logic traced, never deliberately exercised · `[ASSUMED]` = believed, unproven — a flagged landmine.
Surveyed 2026-07-27 against the current tree. Most claims here are `[DERIVED]` (code reading); tags mark the exceptions.

---

### 1. Machine model & PCI glue (`hw/xbox/xbox.c`, `xbox_pci.c`, `acpi_xbox.c`, `lpc47m157.c`)

**Purpose** — The QEMU machine type `xbox` (`TYPE_XBOX_MACHINE`, the binary's default machine): a single
Pentium III with 64 MB RAM (`-m` scalable), the nForce-420-derived chipset (comment block `xbox_pci.c:47-58`),
and the full console device population. `xbox_init_common()` (`xbox.c:210`) builds RAM + flash/boot-ROM,
the PCI fabric, ISA bus with stock PC parts (i8259/i8254/i8257/RTC/PC-speaker), IDE, the SMBus device family,
2× OHCI USB, nvnet, APU, ACI, and NV2A on a dedicated AGP bus.

**Interfaces**
- PCI topology (`xbox.c:260-340`, `xbox_pci.c:160-239`): `00:00.0` host bridge `xbox-pci` (10de:02a5) ·
  `00:01.0` LPC `xbox-lpc` (10de:01b2, ISA bridge + ACPI PM) · `00:01.1` `xbox-smbus` (10de:01b4) ·
  `00:02.0`/`00:03.0` `pci-ohci` (4 ports each) · `00:04.0` `nvnet` · `00:05.0` `mcpx-apu` · `00:06.0` `mcpx-aci` ·
  `00:09.0` `piix3-ide` · `00:00.3` `pci-testdev` (memory-controller stub) · `00:1e.0` `xbox-agp` bridge → NV2A at `01:00.0`.
- IRQ routing is **hard-coded to match retail hardware**: internal-bus nibble table `0x0e065491` at LPC config `0x6C`,
  PIRQ bytes `0x00031000` at `0x68`, ACPI/SMBus IRQs from a routing pair at `0x64` (`xbox_pci.c:74-158`).
  Effective: usb0=1, usb1=9, nic=4, apu=5, aci=6, ide=14, AGP→3.
- ACPI PM (`acpi_xbox.c`): LPC BAR0, 256-byte I/O — PM1 events/control/timer, GPE at +0x20 (len 4, the SMC
  EXTSMI path), GPIO block at +0xC0 (len 26). **Fork hook:** guest write of 5 to GPIO offset 0x16 calls
  `xemu_set_widescreen()` (`acpi_xbox.c:73-75`) — the kernel's aspect-ratio pin drives our display logic.
- Machine properties (`xbox.c:446-505`): `bootrom`, `avpack` (default `hdtv`), `short-animation`,
  `smc-version` (default `P01`), `video-encoder` (default `conexant`).
- `lpc47m157.c` (SMSC super-IO, 2× 16550 UARTs behind a config port at 0x2e, or 0x4e with `sysopt`): the QOM type
  is registered but **nothing instantiates it** — not in machine init, reachable only via `-device lpc47m157` [DERIVED:
  repo-wide grep]. Its serial ports are what a devkit/debug-kernel serial console would use [ASSUMED — untested here].

**Invariants we rely on**
1. The devfn/IRQ layout above is guest ABI — kernels probe fixed addresses; adding a device on the internal bus
   outside the known slots **asserts** in `xbox_lpc_map_irq()` (`xbox_pci.c:141`). [DERIVED]
2. PCI hole aliasing starts exactly at `ram_size` (`xbox_pci.c:194-201`); RAM size changes move it (the 128 MB
   experiment in `docs/perf-roadmap-30-to-60.md` §123 leans on this). [DERIVED]
3. GPE via `xbox_send_gpe()` is the only SMC-interrupt delivery path to the guest. [DERIVED]

**Danger notes** — `xbox-smbus` and `xbox-pci` vmstates are PCI-config-only with `// FIXME` (`xbox_pci.c:281-290`);
ACPI GPIO state is not in vmstate (`acpi_xbox.c:77` FIXME) and `field_pin` is a static global — snapshots skip both.
The widescreen hook means *guest code* flips host display state. `lpc47m157.c` calls `memory_region_destroy()` on
live serial regions during reconfig (`:118-119`) — a known-sharp QEMU pattern.

**Escalation trigger** — any PCI/IRQ topology change, new machine property, RAM-size work beyond experiments,
touching the PM/GPIO map, or actually instantiating the super-IO → full Tier-B spec first.

---

### 2. MCPX boot machinery (`hw/xbox/xbox.c:64-174`, `xbox_pci.c:332-366`, wiring in `system/vl.c:2982-3010`)

**Purpose** — Emulates the MCPX secret boot ROM: the 512-byte mask ROM the southbridge overlays on the top of the
4 GB address space at power-on, which decrypts/validates the flash BIOS and is then hidden. Note the *code* lives in
`xbox.c`/`xbox_pci.c` — the `hw/xbox/mcpx/` directory holds the southbridge peripherals (ACI/APU/nvnet), not this.

**Interfaces**
- Machine property `bootrom=<file>` (from config key `sys.files.bootrom_path`; `vl.c` enforces exactly 512 bytes,
  `xbox.c:150-156` exits if not). Flash BIOS via `-bios` (`sys.files.flashrom_path`), 64 KiB-multiple, loaded at
  `0xFF000000` and mirrored up to 4 GB (`xbox.c:115-123`).
- Mechanism (`xbox_flash_init`): a **writable RAM** region `"xbox.mcpx"`, sized like the BIOS image and overlaid at
  `4GB - bios_size` with priority 1; the user's 512 boot-ROM bytes replace the image's last 512 bytes. Enabled on
  every reset (`xbox_lpc_reset`); the guest hides it by writing bit 1 to LPC config `0x80`
  (`xbox_lpc_config_write` → `memory_region_set_enabled(false)`), a one-way switch until next reset.
- **Microsoft firmware is never committed to this repo — hashes only.** Reference set lives in
  `docs/discord-perf-writeup.md` (§"Firmware"): `mcpx_1.0.bin` 512 B sha1 `5d270675b54eb8071b480e42d22a3015ac211cef`;
  flash `xbox-4627_debug.bin` 1 MiB sha1 `a5503364b2b07b9bb01cb6ef2fb412c25364bb7d`. [VERIFIED — every campaign
  boot log shows `-machine xbox,bootrom=.../mcpx_1.0.bin`, e.g. `docs/perf/loc/baseline-log-excerpt.txt:15`]

**Invariants we rely on**
1. The overlay region is **RAM, deliberately writable**: retail 1.1+ kernels briefly write to ROM addresses and
   read the value back (cache quirk, `sub ds:0FFFFD52Ch` — comment `xbox.c:125-142`). Making it read-only breaks boot. [DERIVED]
2. The overlay is page-aligned by construction (sized = BIOS image) — stated as a performance requirement (`xbox.c:127`). [DERIVED]
3. `xbox_pci.c` finds the region **by the string name `"xbox.mcpx"`** (`xbox_lpc_enable_mcpx_rom`); renaming it in
   `xbox.c` silently kills the disable path. [DERIVED]

**Danger notes** — On `loadvm`, LPC config `0x80` is restored raw (no `config_write` callback, no `post_load` hook),
but reset has already *re-enabled* the overlay: a post-boot snapshot resumes with the boot ROM **visible again** at
the top of RAM even though the guest turned it off. Nothing re-reads that window after early boot, which is why it
goes unnoticed [DERIVED — unexercised; treat as live trap for anything that scans high memory]. Missing/invalid
bootrom is only a soft failure upstream in `vl.c` (error message, boots without overlay → flash must be unsigned/debug).

**Escalation trigger** — touching overlay/enable mechanics, HLE/bootrom-bypass or kernel-shim work, non-512-byte
ROMs, or making the disable state snapshot-correct → Tier-B spec.

---

### 3. APU / DSP audio (`hw/xbox/mcpx/apu/` — `apu.c`, `vp/`, `dsp/`, `dsp/interp/`; subproject `subprojects/dsp56300/`)

**Purpose** — The MCPX Audio Processing Unit: VP (voice processor — up to 256 voices, ADPCM, SVF filters, HRTF,
mixed into 32 mixbins) plus **two Motorola DSP56300-family cores** — GP (global processor) and EP (encode processor)
— that run Microsoft-supplied microcode uploaded by games; EP output is the console's final audio. A monitor stage
plays the result on the host via SDL (48 kHz S16 stereo).

**Interfaces**
- PCI `00:05.0` (10de:01b0), IRQ 5. BAR0 = 512 KiB MMIO (`apu.c:392-407`): `0x00000` global `NV_PAPU_*` regs,
  `0x20000` VP, `0x30000` GP, `0x50000` EP (reg maps in `apu_regs.h`). Guest RAM reached via per-block
  scatter-gather page tables (`dsp/gp_ep.c:scatter_gather_rw`) and a direct `ram_ptr`.
- Config keys (`config_spec.yml:574-588`): `audio.use_dsp` (off → VP-only monitoring, GP/EP not realtime),
  `audio.use_dsp_jit` (default true), `audio.vp.num_workers` (0=auto), `audio.hrtf`, `audio.volume_limit`.
- **DSP core integration**: `dsp/dsp.c` fronts two backends — the C interpreter (`dsp/interp/dsp_cpu.c`) and a
  **Rust JIT** (`dsp/dsp_jit.c`, `#include <dsp56300.h>`). The JIT links `libdsp56300_emu_ffi.a`, a **prebuilt blob**
  fetched at *meson configure time* by `subprojects/dsp56300/meson.build` (curl from
  `github.com/mborgerson/dsp56300` release v0.1.3, keyed on the Rust target triple). The win64 cross build pre-seeds
  `dsp56300-0.1.3-x86_64-pc-windows-gnu.tar.gz` (committed in-tree) — `build-release.sh:72-84` handles this,
  including the `TAR_OPTIONS=--no-same-owner` uid-1001 workaround. `dsp_set_engine()` switches backends live per
  the config key (`gp_ep.c:26-49`).
- Timing: dedicated thread `mcpx.apu_thread` + a VP voice-worker pool (`vp/vp.c:1585+`); frame cadence
  32 samples/VP-frame, EP frame = 256 samples ≈ 5.333 ms (`apu_regs.h:333,363`); paces itself against the SDL
  output queue watermarks (`apu.c:169-217`, `monitor.c:44-59`).

**Invariants we rely on**
1. **Lock order: BQL is dropped before taking `d->lock`** — reset, VM-state-change, and exit all do the
   unlock/relock dance (`apu.c:332-343, 348-375, 434-449`). Reordering deadlocks. [DERIVED]
2. Save/restore only happens with the APU thread quiesced (`mcpx_apu_vm_state_change` + `dsp_sync_to_vm/from_vm`);
   vmstate `mcpx-apu` v1 carries full DSP core memories, and monitor-loadvm benchmarking depends on it restoring
   cleanly. [VERIFIED — the SB/LoC measurement workflow round-trips these snapshots]
3. APU pacing is wall-clock (`QEMU_CLOCK_REALTIME`) and effectively throttles the whole machine when audio output
   stalls — perf work must treat the APU as a pacing source, not a passive device. [DERIVED]

**Danger notes** — `NV_PAPU_FEMEMDATA` "magic write" performs `stl_le_phys` to a guest-controlled address from MMIO
context (`apu.c:85-91`). `scatter_gather_rw` *asserts* on out-of-range SGEs/addresses — guest-triggerable abort.
`g_state` global for debug/monitor handlers. Changing any vmstate field breaks snapshots (the 273-byte
`VMSTATE_UNUSED` tombstone at `apu.c:514` shows the scar tissue). SDL audio init happens inside device realize.
The configure-time blob download makes clean builds network-dependent (mitigated by the pre-seeded tarball).

**Escalation trigger** — any perf lever touching APU pacing/threading, a dsp56300 version bump, vmstate change,
or routing APU audio into VR spatial audio → Tier-B spec.

---

### 4. ACI / AC97 (`hw/xbox/mcpx/aci.c`, shared core `hw/audio/ac97_int.h`)

**Purpose** — The MCPX Audio Codec Interface: the Xbox's AC97 controller, used for the analog/SPDIF passthrough
path (distinct from the APU/DSP pipeline). xemu implements it by wrapping QEMU's AC97 device core
(`AC97LinkState`, refactored by xemu into `ac97_int.h` for reuse) in the Xbox's MMIO-only register layout.

**Interfaces**
- PCI `00:06.0` (10de:01b1), class multimedia-audio, IRQ 6 (`aci.c:87-90`, routing card 1).
- **BAR2 only**: 0x1000 MMIO — NAM (mixer) at +0x000 (0x100 bytes), NABM (bus master) at +0x100 (0x80 bytes)
  (`aci.c:48-67`). The standard PC AC97 I/O BARs 0/1 are deliberately not registered (commented out) — the Xbox
  kernel expects the MMIO layout.
- DMA into guest RAM via `ac97_common_init(&d->ac97, &d->dev, pci_get_address_space(...))`.

**Invariants we rely on**
1. BAR2 layout (NAM at 0, NABM at +0x100) is guest ABI. [DERIVED]
2. ACI shares the `ac97_int` core with QEMU's stock `ac97` device — behavior changes there hit both. [DERIVED]

**Danger notes** — vmstate `mcpx-aci` saves **only PCI config**; the whole `AC97LinkState` (mixer regs, DMA/BM
pointers, FIFO position) is marked `// FIXME` and not snapshotted (`aci.c:71-80`) — a loadvm during AC97 playback
resumes with a device whose guest-visible programming and internal state have diverged [DERIVED — never observed
because our titles drive the APU path]. The `ac97_int` split is a merge-conflict magnet on QEMU upstream syncs.

**Escalation trigger** — adding ACI vmstate, touching `ac97_int`, or including the AC97 path in VR audio plumbing
→ Tier-B spec.

---

### 5a. SMBus host controller (`hw/xbox/amd_smbus.c`, `xbox-smbus` glue in `xbox_pci.c:241-317`)

**Purpose** — The AMD-756-style SMBus host controller inside the MCPX, PCI `00:01.1`. It is the kernel's only path
to the SMC, EEPROM, video encoder, and temp sensor. All transactions execute **synchronously inside the I/O write**
that starts them — there is no delayed completion.

**Interfaces** — BAR1, 32-byte I/O window (`xbox_smbus_realize`): `0x0` GLOBAL_STATUS, `0x2` GLOBAL_ENABLE
(cycle type + HOST_STC start + ABORT), `0x4` HOST_ADDRESS (addr<<1|rd), `0x6/0x7` DATA0/1, `0x8` HOST_COMMAND,
`0x9` BLOCK_DATA (auto-increment index, wraps at 32). Protocols: quick/byte/byte-data/word-data/block
(`amd756_smb_transaction`). Completion sets `GS_HCYC_STS`, errors set `GS_PRERR_STS`; IRQ raised only when
`GE_HCYC_EN` is set, wired to the LPC ACPI-routing byte pair at config `0x64` (`xbox_pci.c:215-226`).

**Invariants we rely on** — (1) synchronous completion: guests may poll status immediately after the kick and
must see the result [DERIVED]; (2) the `I2CBus` named `"i2c"` created here is the bus every card-5 device sits on
(`amd756_smbus_init`, `xbox.c:299-314`). [DERIVED]

**Danger notes** — vmstate is PCI-config-only (`// FIXME`, `xbox_pci.c:281-290`): an in-flight or just-completed
transaction's registers are lost across snapshots [DERIVED — benign in practice since transactions never span an
instruction boundary from the guest's view]. The post-switch `abort()` in `amd756_smb_transaction` is unreachable
but says "here be dragons" about unknown cycle types.

**Escalation trigger** — async/interrupt-driven rework, adding devices to the bus, or vmstate completion → Tier-B.

---

### 5b. SMC — System Management Controller (`hw/xbox/smbus_xbox_smc.c`) — **most load-bearing card**

**Purpose** — Emulates the PIC16LC system-management microcontroller: power/reset control, DVD tray, AV pack
identification, LED/fan, the boot-time challenge handshake, and the scratch register the kernel reads for the
boot-animation skip. SMBus address **0x10**. Every boot talks to it; wrong answers here brick the boot.

**Interfaces — command register map** (defines `:57-96`; write path `smc_write_data`, read path `smc_receive_byte`):

| reg | name | emulated behavior |
|---|---|---|
| 0x01 | VER | cycles the 3-char version string (machine prop `smc-version`, default `P01`); write resets index |
| 0x02 | POWER | write RESET(0x01)/CYCLE(0x40) → `qemu_system_reset_request`; SHUTDOWN(0x80) → shutdown |
| 0x03 | TRAYSTATE | OPEN 0x10 / NO_MEDIA 0x40 / MEDIA 0x60 — derived from the `ide0-cd1` block backend |
| 0x04 | AVPACK | from machine prop `avpack` (SCART 0/HDTV 1/VGA 2/RFU 3/SVIDEO 4/COMPOSITE 6/NONE 7) |
| 0x05/0x06 | FANMODE/FANSPEED | accepted, ignored; reads return 0 |
| 0x07/0x08 | LEDMODE/LEDSEQ | accepted, ignored |
| 0x09/0x0a | CPUTEMP/BOARDTEMP | reads return 0 (real temps come from the ADM1032 at 0x4C) |
| 0x0c | TRAYEJECT | accepted, ignored (UI drives tray state instead) |
| 0x0d | INTACK | accepted, ignored |
| 0x0e/0x0f | ERROR write/read | scratch error code (kernel POST codes) |
| 0x11 | INTSTATUS | bits POWER/TRAYCLOSED/TRAYOPENING/AVPACK_(UN)PLUG/EJECT/TRAYCLOSING; **cleared on read** (`FIXME: Confirm`) |
| 0x19/0x1a | RESETONEJECT / INTEN | accepted, ignored |
| 0x1b | SCRATCH | persisted byte; bit 0x04 = short-animation (seeded from machine prop at realize) |
| 0x1c–0x1f | challenge request | fixed bytes `0x52 0x72 0xea 0x46` |
| 0x20/0x21 | challenge response | writes accepted, not checked |

Out-of-guest interfaces: `xbox_smc_power_button()`, `xbox_smc_eject_button()`, `xbox_smc_update_tray_state()`
(called from UI on user action) — each sets INTSTATUS bits and asserts **EXTSMI** via the ACPI GPE path
(`xbox_assert_extsmi` → `acpi_send_event(ACPI_EXTSMI_STATUS)` → card 1's `xbox_send_gpe`).

**Invariants we rely on**
1. Challenge regs 0x1c–0x1f must return non-zero (these exact) bytes — retail 1.1+ kernels run the PIC challenge
   handshake and panic the boot otherwise (xbox-linux reference cited in-file). [DERIVED]
2. Tray state is keyed to a block backend **literally named `"ide0-cd1"`, with `assert(blk != NULL)`**
   (`:358-360`) — safe only because `vl.c:3089-3095` *always* creates the DVD drive, media or not. A bare
   `qemu-system-i386 -machine xbox` without a cdrom drive aborts here. [DERIVED]
3. `avpack=hdtv` default + `SMC_REG_SCRATCH_SHORT_ANIMATION` are how our launches skip/shape boot video;
   the launcher configs depend on them. [VERIFIED — `avpack=hdtv` visible in campaign launch lines]

**Danger notes** — **The SMC has no vmstate at all** (class_init registers no `dc->vmsd`): scratch, intstatus,
error and tray regs are invisible to snapshots; a fresh-process `-loadvm` gets realize-time defaults from *current
config*, not from the snapshot [DERIVED — unexercised; benign so far because post-boot kernels rarely re-read them].
INTSTATUS clear-on-read is unconfirmed against hardware (in-code FIXME). Fan/LED/temp being ignored means any
game/dashboard hardware-monitor UI shows zeros.

**Escalation trigger** — implementing LED/fan/temp/INTEN semantics, tray automation, SMC vmstate, or changing
reset/shutdown plumbing → Tier-B spec (this card is the first to upgrade if any SMC work is planned).

---

### 5c. Video encoders + temp sensor (`smbus_cx25871.c`, `smbus_fs454.c`, `smbus_xcalibur.c`, `smbus_adm1032.c`)

**Purpose** — Presence stubs for the three board-revision video encoder chips and the ADM1032 temperature sensor.
The kernel probes SMBus to identify which encoder the board has and programs AV output accordingly; **actual video
is produced by NV2A — these devices only have to *exist* and echo registers.** Encoder choice = guest's belief
about the board revision.

**Interfaces** — selected by machine prop `video-encoder` (`xbox.c:302-314`): `conexant` → CX25871 at **0x45**
(default) · `focus` → FS454 at **0x6A** · `xcalibur` → Xcalibur at **0x70**. ADM1032 at **0x4C** is instantiated
*only* in the non-Xcalibur case (matches 1.6 boards where the SMC took over temps). All three encoders are
identical 256-byte register sponges (write stored at cmd offset, read back with auto-increment); the ADM1032
returns 50 (°C) for reg 0x00 (local) and 0x01 (remote), 0 for everything else.

**Invariants we rely on**
1. Exactly one encoder answers on the bus per boot; the kernel's probe order finds it and moves on. [DERIVED]
2. Kernel-version ↔ encoder pairing: 1.6-era kernels expect Xcalibur and no ADM1032. [ASSUMED — we run
   `conexant` everywhere; `xcalibur` boots have never been tested in this fork]
3. ADM1032's constant 50 °C keeps thermal logic quiet (no fan panic, no overheat shutdown). [DERIVED]

**Danger notes** — None of the four has vmstate: sponge contents vanish across snapshots (benign — kernels probe
at boot only [DERIVED]). Register writes have zero side effects, so anything reading *status* bits back (macrovision
flags, field sync) sees only what it previously wrote or zeros — a silent divergence source if a title polls the
encoder. `acpi_xbox.c`'s GPIO "field pin from tv encoder?" toggle is the one place encoder-ish timing leaks in.

**Escalation trigger** — making encoder registers *do* anything (widescreen/interlace detection, AV-mode
switching), testing/supporting `xcalibur`, or live temp data → Tier-B.

---

### 5d. EEPROM: storage device + identity generation (`smbus_storage.c`, `eeprom_generation.{c,h}`)

**Purpose** — The 256-byte config EEPROM at SMBus **0x54**: the console's identity (serial, MAC, region,
HDD key, online key) plus user settings (timezone, language, video flags). `smbus-storage` is a generic
file-backed SMBus memory with write-through persistence; `eeprom_generation.c` mints a brand-new identity
when no file exists.

**Interfaces**
- Instantiated by the frontend, not the machine: `vl.c:3030-3038` always adds
  `-device smbus-storage,file=<sys.files.eeprom_path>` (file must be exactly 256 bytes, `vl.c` enforces;
  auto-generates via `xbox_eeprom_generate(path, XBOX_EEPROM_VERSION_R1)` if missing). Device props:
  `addr` (default 0x54), `size` (256), `persist` (true), `file`. [VERIFIED — launch log
  `docs/perf/loc/baseline-log-excerpt.txt:15` shows the device line]
- Layout (`eeprom_generation.h:29-44`): `hash[20] · confounder[8] · hdd_key[16] · region · checksum ·
  serial[12] · mac[6] · pad · online_key[16] · video_standard · pad · user_checksum · user_section[156]`.
- Generation (`eeprom_generation.c:93-140`): security section RC4-encrypted with a key from a double-SHA1 whose
  IVs are **kernel-version-specific** (versions D/R2/R3/default-1.0 tables, from the xbox-linux "Middle Message"
  paper); random `hdd_key`/`online_key`/confounder; MAC = Microsoft OUI `00:50:F2` + 3 random bytes; random
  numeric serial; defaults: region NA (1), `video_standard` NTSC-M `0x00400100`, GMT timezone, English.

**Invariants we rely on**
1. Persistence is *whole-file write-back on any guest write* (`smbus_storage_write_data:111-125`) — the file on
   disk always matches the last guest write. [DERIVED]
2. EEPROM version passed at generation must match the kernel family (vl.c uses R1 = retail 1.0 kernels; our debug
   4627 kernel also accepts it). [VERIFIED for our firmware set — boots; other kernels ASSUMED]
3. `hdd_key` ↔ ATA-lock coupling: the kernel locks/unlocks the HDD with a key derived from the EEPROM, so
   regenerating the EEPROM under a *locked* HDD image would strand it. [ASSUMED — our qcow2 images are unlocked;
   never exercised]
- Citation note: the spec plan files `docs/features/identical-device-identity.md` under this card — **that doc is
  actually about telling identical *input* devices (twin T.16000M sticks) apart**, not EEPROM identity. Flagged
  rather than silently re-cited; the EEPROM generated-identity feature is fully described here.

**Danger notes** — Snapshot/file divergence: vmstate stores `data[]` + `offset`, but the backing file keeps its
newest content — a `loadvm` rewinds RAM state while the file stays ahead; the *next* cold boot then differs from
the restored session [DERIVED]. Guest writes trigger synchronous host file I/O on the iothread. A corrupted/
regenerated EEPROM changes MAC/serial — anything doing System-Link or per-console identity sees a "new console".

**Escalation trigger** — identity management features (region switching, serial/MAC control, per-user EEPROMs),
encryption-version selection UI, or making persistence transactional → Tier-B.

---

### 6. Network — nvnet (`hw/xbox/mcpx/nvnet/nvnet.c`, `nvnet_regs.h`; frontend `ui/xemu-net.c`)

**Purpose** — The nForce Ethernet controller (forcedeth-family), PCI `00:04.0` (10de:01c3). A descriptor-ring DMA
NIC with an emulated MII PHY; the guest kernel drives rings in RAM, the model moves frames between those rings and
a QEMU netdev backend.

**Interfaces**
- BAR0 = 0x400 MMIO (`nvnet.c:35-36`): IRQ_STATUS/MASK, TX/RX ring base + `NVNET_RING_SIZE`, next-descriptor
  pointers, TX/RX control+status, PACKET_FILTER, MAC/multicast addr+mask, MDIO_ADDR/DATA (PHY at addr 1),
  ADAPTER_CONTROL/LINKUP. BAR1 = 8-byte I/O stub (reads 0). INTA → IRQ 4. `RingDesc` =
  `{buffer_addr u32, length u16, flags u16}` packed (`nvnet.c:80-84`).
- Attach: QEMU NIC (`qemu_new_nic` in realize; machine `default_nic = "nvnet"`, created at `xbox.c:326-328` via
  `qemu_configure_nic_device`). The **frontend** (`ui/xemu-net.c`) creates the backend from config
  `net.enable` / `net.backend` ∈ {`nat` (slirp + `net.nat.forward_ports` hostfwd), `udp`, `pcap`} as netdev
  `"xemu-netdev"` + hubport, and toggles the link with `qmp_set_link("nvnet.0", …)` — so the NIC's QOM id is ABI
  to the UI. [DERIVED]
- RX gating: `rx_enabled && dma_enabled && link-up && descriptor-available` (`nvnet_can_receive`); filter:
  broadcast always accepted (bcast filtering FIXME), else MYADDR filter matches the **guest-programmed**
  `NVNET_MAC_ADDR_A/B` / multicast+mask regs (`receive_filter:561-610`). TX gathers multi-descriptor frames into
  a bounce buffer; MII autoneg completes on a 250 ms virtual timer.

**Invariants we rely on**
1. The MAC that matters is the one the *guest* writes into `NVNET_MAC_ADDR_*` (the kernel copies it from the
   EEPROM — card 5d) — not the QEMU-side `NICConf` default. [DERIVED]
2. Ring next-descriptor pointer registers are the authoritative progress state (v2 snapshot format). [DERIVED]
3. Link state survives loadvm by *inference from PHY BMSR*, or restarts autoneg mid-negotiation (`nvnet_post_load`). [DERIVED]

**Danger notes** — vmstate v2 with a v1 migration shim (reconstructs next-desc pointers from deprecated indices,
resets PHY regs) — old snapshots load but behave subtly differently; don't touch the field list casually.
Oversized frames (> MTU+128) are silently dropped. `qmp_set_link` name-couples to `"nvnet.0"`. The `pcap` backend
loads a pcap DLL/so at runtime (`xemu-net.c:70`).

**Escalation trigger** — System Link / online work, checksum-offload or perf changes to the DMA paths, or a second
NIC instance → Tier-B.

---

### 7. XBLC — Xbox Live Communicator (`hw/xbox/xblc.c`)

**Purpose** — Determined: it is the **voice headset dongle** that slots into a controller's expansion port — a USB
device (VID 045e, PID 0283) with two isochronous endpoints: EP4 OUT = game→headset speaker audio, EP5 IN =
microphone capture. 16-bit mono PCM, default 16 kHz. The model bridges both directions straight to the host's
default SDL playback/recording devices — a real host-mic voice path, not a stub.

**Interfaces**
- Descriptors (`xblc.c:40-145`): vendor interface class 0x78 (XID-adjacent), 2 interfaces × 1 isoc endpoint,
  `wMaxPacketSize` 48, bcdDevice 0x0110. Control: vendor `SET_FEATURE` index 0 = sample rate by table index
  {8000, 11025, 16000, 22050, 24000}; index 1 = AGC (accepted, ignored). Everything else STALLs.
- Data path: OUT packets → SDL playback stream; IN packets ← SDL recording stream, with a 100 ms max queue
  (over-threshold clears the stream to bound latency, `:247-253`).
- **Reachability:** the type `usb-xblc` is compiled and registered, but the UI peripheral system knows only
  `PERIPHERAL_XMU` (`ui/xemu-input.h:165`) — nothing in-tree instantiates XBLC; it is reachable only by passing
  extra QEMU args (`-device usb-xblc,port=…` on an OHCI port/daughterboard-hub port). [DERIVED — repo-wide grep]

**Invariants we rely on**
1. Descriptor shape (class 0x78, EP4/EP5 isoc, size 48) is what the kernel voice driver enumerates — cosmetic
   changes break title-side detection. [DERIVED, per Ryzee119 hardware notes cited in-file]
2. Sample-rate changes rebuild both SDL stream formats atomically (`xblc_set_sample_rate`). [DERIVED]

**Danger notes** — Realize **opens the host microphone** (privacy surface — first-class concern if we ever expose
this in PenguinBox UI). SDL failures degrade to warnings, leaving `s->in/out` NULL (silent device). vmstate v2
restores only `sample_rate` (post_load reformats streams); in-flight audio is lost. Isoc pacing rides OHCI frame
timing — emulation speed wobble = voice artifacts.

**Escalation trigger** — exposing XBLC in the peripheral picker, VR voice-chat integration, or changing the audio
transport → Tier-B (plus an explicit privacy/consent design note).

---

### 8. Chihiro — arcade variant (`hw/xbox/chihiro.c`, `chihiro-usb.c`)

**Purpose** — Sega's Chihiro arcade platform (Xbox-derived board + Sega baseboard/mediaboard). The sources model:
machine type `chihiro` reusing `xbox_init_common`; an IDE-mapped mediaboard interface (boot ROM
`fpr21042_m29w160et.bin`, 128 MB board-RAM "filesystem" loaded from a GD-ROM-style image); `chihiro-lpc` ISA
device at I/O 0x4000 answering SEGA_CHIP_REVISION (ASIC) and DIMM size (128M); and `an2131qc`/`an2131sc` USB
stubs for the JVS I/O baseboard.

**Interfaces (as written)** — machine props `mediaboard-rom`, `mediaboard-filesystem`; default CPU literally
`486`; `chihiro-lpc` 2-byte-wide I/O reads at 0x40F0/0x40F4.

**Reachability — the operative fact: NOT BUILT.** Both files are commented out of the build
(`hw/xbox/meson.build:4-5`). [VERIFIED] Independently, the sources have bit-rotted against this QEMU generation
(`sysemu/*.h`, `exec/memory.h`, `hw/kvm/clock.h` includes; `pcmc->default_nic_model` no longer exists) so they
would not compile if re-enabled [DERIVED]; and even in their own era the core piece was unfinished —
`chihiro_ide_interface_init` ends in `assert(0)` "Chihiro IDE not yet implemented" (`chihiro.c:271-274`).
**There is no `-machine chihiro` in this fork's binary; nothing PenguinBox ships can reach this code.**

**Invariants we rely on** — none. The live system has zero dependencies on these files; they are frozen reference
material for the arcade-board interface layout.

**Danger notes** — `chihiro_init` calls `xbox_init_common`, so if ever revived it silently inherits every machine
change made since (SMC avpack default 0 "for Chihiro" at `smbus_xbox_smc.c:262` is the one surviving trace of that
coupling). Do not "fix" the includes casually — reviving it is a port, not a patch.

**Escalation trigger** — re-enabling either file in `meson.build` (i.e., any attempt to boot Chihiro titles)
→ full Tier-B spec *and* an API-port plan first; there is no incremental path.

---

### 9. USB XID input devices (`hw/xbox/xid.c`, `xid.h`, `xid-gamepad.c`, `xid-steel-battalion.c`) — POINTER CARD

**This card is deliberately a stub.** The input stack is **Tier B, actively modified by us** — the full as-built
spec is **[`docs/internal/B6-input-stack.md`](B6-input-stack.md)** (being written in parallel; do not duplicate here).

One-paragraph orientation only: `xid.c`/`xid.h` implement the shared XID (Xbox Input Device) USB protocol and
descriptor plumbing; `xid-gamepad.c` provides `usb-xbox-gamepad` (045e:0202) and `usb-xbox-gamepad-s` (045e:0289);
`xid-steel-battalion.c` provides `usb-steel-battalion` (Capcom 0a7b:d000) — the VR flagship device (SBC docs under
`docs/features/sbc-*.md`). Devices attach to the OHCI root/daughterboard hub (`vl.c` adds
`usb-hub,port=1,ports=4`) and are bound/hotplugged by `ui/xemu-input.c` from SDL input. For interfaces,
invariants, scancode maps, binding resolution, hotplug/vmstate behavior and every danger note: **see B6.**

**Escalation trigger** — already escalated; B6 is the spec of record. Any change here lands against B6, not this card.
