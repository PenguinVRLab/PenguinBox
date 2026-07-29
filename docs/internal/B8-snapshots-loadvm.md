# Spec — Snapshots (savevm/loadvm)

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| Tier | Code | Status | Last verified |
|---|---|---|---|
| B | `migration/savevm.c` (inherited core, XBOX hooks), `ui/xemu-snapshots.{c,h}`, `ui/xui/snapshot-manager.cc`, `hw/xbox/nv2a/nv2a.c` (vmstate + savevm hooks), `hw/xbox/nv2a/pgraph/vk/{renderer,surface,texture}.c`, `hw/xbox/xid-steel-battalion.c`, `system/vl.c` | v1 draft | 2026-07-27 |

## Purpose & scope

Whole-machine save/restore: QEMU vmstate serialization embedded in qcow2
internal snapshots, exposed through HMP (`savevm`/`loadvm`), the CLI
(`-loadvm`), and the xui snapshot UI. In this fork it is simultaneously a user
feature and **the perf campaign's primary instrument**: a `savevm` of a
worst-case scene converts every A/B into a deterministic ~90-second run
(`docs/perf/loc/02-lessons-learned.md` §3.1/§3.3).

Out of scope: live migration and the QMP `snapshot-save`/`-load` jobs
(upstream, `qapi/migration.json:2500`, unused here); record/replay; guest-side
game saves (guest FS content, not vmstate — though `loadvm` rolls the HDD
back too, see I10).

## External interface (monitor commands, CLI flags, UI, file locations)

**HMP monitor** (`hmp-commands.hx:331-386`, `hmp-commands-info.hx:382`):
`savevm [tag]` (create/replace; no tag ⇒ auto-name `vm-%Y%m%d%H%M%S` — the
rig's `vm-20260725222138` / `vm-20260727054737` are these), `loadvm tag`,
`delvm tag`, `info snapshots`. Reached via (a) the in-app Monitor window —
Debug menu, `~` toggle (`ui/xui/menubar.cc:252`); (b) a monitor socket, the
rig pattern: launch with `-monitor unix:/tmp/xemu-mon.sock,server,nowait`,
then `printf 'loadvm TAG\n' | nc -U /tmp/xemu-mon.sock` (use `nc -q 8`;
without `-q` nc hangs on the never-closing socket —
`runbooks/save-state-testing.md`).

**CLI**: `-loadvm '<name>'` (`system/vl.c:3631` → `vl.c:2882`
`load_snapshot(..., &error_fatal)`), executed immediately after
`qemu_machine_creation_done()` — i.e. **before** the UI thread hotplugs input
devices; this is why it fails with the SBC (F1). Quote names (spaces).

**UI** (one paragraph — the frontend itself belongs to the Tier-C UI card
"C2", `ui/xui` scenes/menus/snapshot-manager, planned in
`00-specification-plan.md` §2): the Snapshots page (`ui/xui/main-menu.cc:1393-1543`)
lists snapshots with XBE title + 160×120 thumbnail, creates (typed name),
loads, deletes, and binds slots; Machine menubar and controller popup offer
quick save (`menubar.cc:111`, `popup-menu.cc:439`); **F5–F8 load /
Shift+F5–F8 save** the bound snapshots (`main.cc:304-315` → `actions.cc:94`),
slot→name mapping in config `general.snapshots.shortcuts.f5..f8`. Fork delta:
the F-keys are **disabled while the keyboard drives a Steel Battalion
controller** (ISS-B12 — they become cockpit toggles; the menu still works).
`SnapshotManager::LoadSnapshotChecked` (`snapshot-manager.cc:59`) compares the
snapshot's recorded disc path against the mounted disc and raises a
mount/load-anyway/cancel modal on mismatch — a UI-only courtesy; monitor/CLI
loads skip it.

**File locations**: **no loose savestate files.** The vmstate blob and the
internal snapshot live inside the qcow2 at `sys.files.hdd_path`
(`bdrv_all_find_vmstate_bs` picks the only writable snapshot-capable drive =
the HDD; the DVD is a read-only raw ISO). Offline inventory: `qemu-img
snapshot -l <hdd.qcow2>` (non-destructive; the listed *name* is the loadvm
tag). Rig image: `~/xemu-assets/xbox_hdd.qcow2` (`runbooks/save-state-testing.md`).
Sharing a state = sharing/importing an HDD image (use a separate toml for
imports, never repoint the working `hdd_path`).

**xemu extra data**: `qemu_savevm_state` prepends (savevm.c:1817, `#ifdef
XBOX`) a blob before QEMU's own stream header: magic `0x78656d75` ('xemu'),
version 1, then disc path, XBE title name, thumbnail PNG
(`xemu-snapshots.c:260-302`). Skipped on load (savevm.c:3049); magic absent ⇒
rewind 4 bytes and proceed, so stock-QEMU-made snapshots load
(`xemu-snapshots.c:304`). The UI parses the blob by re-opening the HDD
read-only with `BDRV_O_RO_WRITE_SHARE` (`xemu-snapshots.c:133-176`).

## State & data structures (what is serialized; the vmstate inventory relevant to us)

**Generic sections**: `ram` — one `xbox.ram` block, 64 or 128 MiB per
`sys.mem_limit` (`config_spec.yml:617`). The Xbox is UMA: **NV2A VRAM *is*
this RAM** (`nv2a.c:211-216`; `vga.vram` aliases it) — every framebuffer,
texture, and vertex buffer the guest can see is serialized here. Plus `timer`
(QEMU_CLOCK_VIRTUAL), CPU, and PCI/IDE/USB core state.

**hw/xbox device vmstate inventory** (grep `VMStateDescription` in `hw/xbox`):

| vmsd name | version (min) | file | note |
|---|---|---|---|
| `nv2a` | **3 (1)** | `nv2a/nv2a.c:457` | see below |
| `nv2a/pgraph/vertex-attr` | 1 | `nv2a.c:447` | **EMPTY** — `// FIXME`, VertexAttribute shadow state not serialized |
| `xbox-pm`, `xbox-smbus`, `xbox-lpc`, `lpc47m157`, `mcpx-aci`, `smbus-storage` | 1 | various | machine/ACPI/SMBus/EEPROM |
| `mcpx-apu` + dsp subsections | 1 | `mcpx/apu/apu.c` | audio |
| `nvnet` | 2 (1) | `mcpx/nvnet/nvnet.c:1032` | has `post_load` v1 upgrade |
| `usb-xbox-gamepad` / `-s` | 1 / 0 | `xid-gamepad.c` | core USB state only (`// FIXME`) |
| `usb-xblc` | 2 (1) | `xblc.c:364` | |
| `usb-steel-battalion` | **1** | `xid-steel-battalion.c:430` | `VMSTATE_USB_DEVICE` core **only** — SBC `in_state`/`out_state` packets NOT serialized (`// FIXME`) |

**`vmstate_nv2a` contents** (`nv2a.c:457-581`): PCI + VGA common state; **the
full raw PGRAPH register file `pgraph.regs_[0x2000]`** (32 KiB) and
`pfifo.regs[0x2000]`, `pvideo`/`pfb`/`ptimer`/`pcrtc`/`pramdac`/`puserdac`
regs; DMA object addresses; `surface_shape`/`last_surface_shape`;
`surface_color`/`surface_zeta` **pgraph-side** state (`offset`, `pitch`,
`draw_dirty`, `buffer_dirty`, `write_enabled_cache`); transform
`program_data`, `vsh_constants`(+dirty), `ltctxa/b`/`ltc1`(+dirty); inline
vertex arrays (v3-extended lengths via `VMSTATE_*_SUB_ARRAY_V`);
`draw_arrays_*`; `waiting_for_flip/nop/context_switch`. **NV2A pgraph register
state IS vmstate'd** — raw registers plus the derived method shadows.

**NOT serialized — the entire renderer**: **no `VMStateDescription` exists
under `hw/xbox/nv2a/pgraph/`** (VK, GL, null — grep returns zero).
`SurfaceBinding`/`TextureBinding` (`pgraph/vk/renderer.h:125-286`),
shader/pipeline/compute caches, storage buffers, all of `PGRAPHVkState`:
host-session state, rebuilt from guest RAM. In particular the 2026-07 perf
fields — `SurfaceBinding.feedback_mode` (:146), `.flushed_draw_time` (:150),
`.image_layout` (:184), `TextureBinding.copied_frame_time` (:278), `.borrowed`
(:279) — appear in **no** VMSTATE macro anywhere in the tree (V2).

**Savevm sequence** (`nv2a.c:395-431`, `savevm.c save_snapshot`):
`vm_stop(RUN_STATE_SAVE_VM)` → the nv2a vm-change handler halts the pfifo
pusher and triggers `pgraph_vk_download_dirty_surfaces` on the pfifo thread
(`vk/surface.c:721`): a pending async-reports marker/queue first forces a full
finish (else queued report writes serialize *out of* the snapshot and the
restored guest spins forever — the S3 coordinator fix), then every
`draw_dirty` surface is downloaded to guest RAM; the handler *waits* for
completion **before** serialization. Then extra data + sections are written,
`bdrv_all_create_snapshot` embeds the vmstate in the qcow2 and snapshots the
drive, and the VM resumes.

**Loadvm sequence**: `vm_stop(RUN_STATE_RESTORE_VM)` (halts pfifo) →
`load_snapshot` (`savevm.c:3396`): `bdrv_all_goto_snapshot` (disk rollback
**first**), then `qemu_system_reset(SHUTDOWN_CAUSE_SNAPSHOT_LOAD)` — nv2a
reset sets `flush_pending` and *waits* (`nv2a.c:294-311`) for
`pgraph_vk_flush` (`vk/renderer.c:91`): finish + retire present-ring slots;
`pgraph_vk_surface_flush` (`vk/surface.c:2356`) download-if-dirty then
**invalidate every SurfaceBinding** (unbind targets, unregister CPU access
callbacks, `surface_generation++` each, prune); mark **all** textures
possibly-dirty across whole VRAM (`use_exact_spans=false` — true full
invalidate); re-upload the vertex RAM shadow buffer. Then sections load (RAM,
then devices; `nv2a_pre_load` locks the fifo), `nv2a_post_load` (`nv2a.c:439`)
sets `flush_pending` **again**, and on resume the pfifo thread re-runs
`pgraph_vk_flush` — the second flush finds an empty surface list and
re-uploads the vertex buffer from *restored* RAM.

**Post-load reconstitution** (the GPU-cache story, mechanically):
- **Surfaces: destroyed, rebuilt lazily.** The reset-time flush empties the
  cache; on the next draw `update_surface_part` (`vk/surface.c:2006`)
  recreates bindings because `!current_binding` — independent of the restored
  `buffer_dirty` flags — and uploads content from restored RAM.
- **Textures: retained, revalidated by content hash.** Every LRU node is
  `possibly_dirty`; the next bind rehashes the bytes in restored RAM and keeps
  the cached `VkImage` iff the hash matches, else re-uploads
  (`vk/texture.c:2397-2440`). Borrowed/zero-copy nodes additionally fail the
  `borrow_gen != surface_generation` staleness gate (generation bumped per
  invalidated surface) and release their resources (`texture.c:2404-2414`).
- **Shaders/pipelines: untouched** — persist for the process lifetime (the
  pre-shutdown writeback is commented out, `vk/renderer.c:310-319`); a fresh
  process + loadvm pays compile warmup.
- The `post_load` hook itself only sets `flush_pending` and unlocks the fifo;
  all reconstitution is the flush + lazy rebuild above — there is no
  dirty-tracking replay of restored RAM into the renderer.

## Invariants (numbered I1..In; testable; tagged)

- **I1** [DERIVED] Every savevm is preceded by a synchronous
  download-dirty-surfaces pass: at serialization time guest RAM is
  authoritative for all pixels; no VkImage/host-cache content is ever
  serialized (`nv2a.c:395-407`, `vk/surface.c:721-747`).
- **I2** [DERIVED] A loadvm passes through `qemu_system_reset` →
  `pgraph_vk_flush` *before* any section is read: the surface cache is already
  empty when restored RAM appears, so no stale pre-load surface pixels can be
  downloaded over it. (`load_snapshot` order; `nv2a.c:294-311`)
- **I3** [VERIFIED] No renderer-side state is vmstate'd: `grep -rn
  VMStateDescription hw/xbox/nv2a/pgraph/` → zero matches. The only NV2A vmsd
  is `vmstate_nv2a` (+ the empty vertex-attr struct) in `nv2a.c`.
- **I4** [VERIFIED] The 2026-07 perf fields (`feedback_mode`, `image_layout`,
  `flushed_draw_time`, `copied_frame_time`, `borrowed`) appear in **no**
  VMSTATE entry tree-wide (Verified-by V2); `vmstate_nv2a` is still
  `version_id = 3` — the perf campaign changed the snapshot format not at all.
- **I5** [DERIVED] `loadvm` resolves every stream section by exact
  `(idstr, instance_id)` against the *currently registered* SaveStateEntry
  list (`find_se`); a device recorded in the snapshot but not yet realized in
  this process fails the whole restore with `Unknown section or instance '%s'
  %u` (`savevm.c:2630-2638`).
- **I6** [VERIFIED] The SBC (and every xemu input device) is **hotplugged**:
  `qdev_device_add` from `xemu_input_bind` (`ui/xemu-input.c:1044-1157`) on
  the SDL thread after display init — while CLI `-loadvm` runs during
  `qemu_init` right after machine creation (`vl.c:2882`). Hence CLI `-loadvm`
  of an SBC-bound state fails (`Unknown section or instance
  'pci.0:02.0/1.3/usb-steel-battalion' 0`) and monitor loadvm after boot
  succeeds. Field-reproduced both ways with identical config
  (`save-state-testing.md` FIELD HIT 2026-07-25, workaround proven 07-26;
  `rig-perf-measurement.md` trap 8).
- **I7** [DERIVED] The section idstr is `<qdev path>/<vmsd name>`
  (`vmstate_register_with_alias_id`): OHCI at PCI 02.0 + USB port path +
  `usb-steel-battalion`; `port_map = {3,4,1,2}` and the SBC binds hub-less at
  `1.N` (Duke/S sit behind a hub at `1.N.1`, `xemu-input.c:1106-1137`) —
  player port 1 ⇒ `pci.0:02.0/1.3/...`. Consequence: the SBC must be bound to
  the **same player port** as at save time, or the idstr differs and the same
  Unknown-section failure hits (port-move never deliberately exercised).
- **I8** [DERIVED, observed effect VERIFIED] A failed load is not a no-op:
  disks were already rolled back and the machine reset before section parsing
  — observed as "restore aborts, machine boots fresh", which *looks* launched.
  Verify you are in the saved scene before trusting a load
  (`save-state-testing.md`).
- **I9** [DERIVED] Determinism contract of a loadvm: **restored exactly** —
  all RAM (incl. every framebuffer/texture, UMA), CPU state, vmstate'd device
  registers incl. the full PGRAPH reg file, QEMU_CLOCK_VIRTUAL, RTC device
  state (mc146818, `xbox.c:279`). **Not deterministic** — host wall clock; TCG
  speed and thread interleaving (no icount ⇒ instruction↔vblank alignment
  varies); GPU-side cache warmth (first post-load seconds pay texture
  rehash/re-upload, a fresh process pays shader compiles); live host input
  from the instant of restore (input packets are not even serialized — I11).
  Net: **scene-level determinism, not cycle-level** — the same guest logic
  state replays; timing/FPS is the measured variable.
- **I10** [DERIVED] `loadvm` rolls the HDD back to the snapshot point
  (`bdrv_all_goto_snapshot`) — guest saves written *after* the snapshot are
  rewound for that session. Snapshots are per-HDD-image; the DVD ISO is not
  snapshotted (read-only, excluded) and must still be present/matching.
- **I11** [DERIVED] SBC input report state (`in_state`/`out_state`) is not in
  `vmstate_usb_sb` (only `VMSTATE_USB_DEVICE` core; `xid-steel-battalion.c:430`
  `// FIXME`) — post-load button/axis state comes from live host polling
  within a frame; idle-input replays therefore behave.
- **I12** [VERIFIED] Scene-level replay stability is real: the near-death save
  `vm-20260725222138` self-fails to the end screen in ~19-20 s **every run**
  (trap 2, `rig-perf-measurement.md`) — deterministic enough to be a
  contamination hazard: the flip side of the benchmark guarantee.

## Policies & decision points

- **Vmstate placement** [DERIVED]: `bdrv_all_find_vmstate_bs` → the first
  writable snapshot-capable drive = the HDD qcow2; a raw (non-qcow2) HDD would
  make savevm impossible (`bdrv_all_can_snapshot` gate). HMP and UI saves pass
  `overwrite=true` — same-name snapshots are replaced, not duplicated.
- **Halt discipline** [DERIVED]: NV2A halts/flushes via the VM run-state
  change hook, deliberately *not* `pre_save` — "halt the FIFO before any VM
  state is saved/restored to avoid corruption" (`nv2a.c:392`); the vmsd hooks
  only manage the fifo lock + `flush_pending`.
- **Async-reports pre-save finish** [DERIVED]: pending report marker/queue ⇒
  forced full finish before download, else the snapshot loses in-flight report
  writes and the restored guest deadlocks (`vk/surface.c:723-737`). Rare path;
  correctness over cost.
- **Texture survival policy** [DERIVED]: loadvm never destroys owned texture
  nodes — all marked possibly-dirty, the bind-path content hash decides reuse
  vs re-upload; borrowed nodes are additionally generation-fenced. Surfaces
  get the opposite policy (unconditional invalidation) because their content
  lives GPU-side and their RAM shadow was just replaced.
- **UI disc-mismatch dialog** [DERIVED]: UI loads only (`LoadSnapshotChecked`);
  monitor/CLI loads perform no disc check (Unknown 3).
- **Benchmark methodology** [VERIFIED as practice]: savevm a worst-case scene
  once; every A/B then = cold boot → wait ~18 s → monitor `loadvm` → fixed
  ~90 s counter window → verdict. This produced the LoC round 2–3 verdicts on
  the identical storm scene and a ~10-minute hypothesis-to-data cycle
  (`02-lessons-learned.md` §3.1/§3.3, `00-baseline.md:199-221`). The ~18 s is
  empirical margin — mechanically only "after `xemu_input_init` has bound the
  SBC" is required (I6). Choose representative saves: trap 2's near-death
  state measured a false 30 fps.

## Failure modes

- **F1 — CLI `-loadvm` + hotplugged SBC** (trap 8): dies with `Unknown
  section or instance ...usb-steel-battalion` per I5/I6. Fix: monitor-socket
  loadvm after boot (`printf 'loadvm TAG\n' | nc -U /tmp/xemu-mon.sock` — the
  `loc-bench-*.sh` launchers do this; `rig-perf-measurement.md:66-70`).
- **F2 — Silent fresh boot after failed load** (I8): the process looks
  launched; verify the scene (surface dump / `lane4-grab.sh`) before trusting
  any measurement.
- **F3 — Version mismatch**: stream `version_id` > binary's for a section ⇒
  `unsupported version %d for '%s' v%d` (`savevm.c:2643`). Old binaries refuse
  new snapshots cleanly; new binaries accept back to `minimum_version_id`
  (nv2a: 1).
- **F4 — Unversioned field drift**: adding/removing/resizing vmstate fields
  without a version bump misparses the stream — typically `Missing section
  footer` (`savevm.c:2586`) or an instance load error. The cross-binary
  hazard; the 2026-07 work avoided it by adding zero vmstate fields (I4).
- **F5 — RAM geometry mismatch**: `sys.mem_limit` 64 vs 128 changes
  `xbox.ram` length; `parse_ramblock` tries `qemu_ram_resize` and fails for
  the fixed block (`migration/ram.c:4222`). Same-config discipline applies.
- **F6 — Device-tree drift across builds**: community states usually load
  (SBC users build near-cutting-edge, `save-state-testing.md`), but a
  materially different device tree fails per I5 — suspect build drift before
  the game.
- **F7 — SBC port moved between save and load** (I7): same Unknown-section
  signature as F1 despite the device existing. [DERIVED]
- **F8 — Stock-QEMU interop**: xemu snapshots start with the 'xemu' extra-data
  magic, not QEMU's file magic — stock `qemu-system-*` cannot load them
  [DERIVED]; the reverse direction works (magic-absent rewind).
- **F9 — Measurement contamination post-load**: the first seconds include
  texture-rehash/upload and (fresh process) shader-compile warmup — start
  counter windows after a settle (`tool-specs.md:189` "same snapshot + fixed
  settle"); and trap 2: a self-failing save ends the scene inside long windows.

## Instrumentation

- Inventory: `qemu-img snapshot -l <hdd.qcow2>` offline/read-only (first move
  before touching config); HMP `info snapshots` live; the UI Snapshots page
  shows XBE title + thumbnail (= the framebuffer at save time) per entry.
- Load-boundary observables: `trace_nv2a_pgraph_surface_invalidated` fires per
  binding at the reset flush; NV2A profiler counters show a post-load texture
  rehash/upload burst; phase timers resume with the restored scene's signature;
  the Video Debug FPS overlay persists across loadvm (`rig-perf-measurement.md:13`).
- Scene verification after load: `lane4-grab.sh` screenshot or a surface
  dump; never trust "it's running" (F2).

## Verified-by

- **V1** — No renderer vmstate:
  `grep -rn VMStateDescription hw/xbox/nv2a/pgraph/` → 0 hits (2026-07-27).
- **V2** — Perf fields in no vmstate:
  `grep -rn VMSTATE --include='*.c' --include='*.h' . | grep -E 'feedback_mode|image_layout|flushed_draw_time|copied_frame_time|borrowed'`
  → 0 hits (2026-07-27). Fields exist only in
  `pgraph/vk/renderer.h:146,150,184,278,279`.
- **V3** — `vmstate_nv2a` `.version_id = 3` (`nv2a.c:459`); versioned tails
  `VMSTATE_UINT32_V(...clip_y..., 2)` and `..._SUB_ARRAY_V(..., 3)` are the
  only versioned fields — no v4 introduced by the 2026-07 campaign.
- **V4** — F1 both directions in the field: CLI failure reproduced with the
  exact saving config (2026-07-25), monitor loadvm proven (2026-07-26) —
  `runbooks/save-state-testing.md`; institutionalized as trap 8
  (`rig-perf-measurement.md:66-70`) and failure #7 (`02-lessons-learned.md`).
- **V5** — Benchmark determinism in practice: identical-scene A/B tables on
  `vm-20260727054737` across nine builds (`docs/perf/loc/00-baseline.md:199`)
  and the flicker A/B on `vm-20260725222138` (`docs/perf/flicker-detect.md:102`).
- **V6** — Trap-2 replay stability: the near-death save self-fails in ~19-20 s
  every run (`rig-perf-measurement.md:29-35`).
- Not yet deliberately exercised: F7 (port move), F8 (stock-QEMU cross-load),
  F5 (mem_limit mismatch), post-load behavior with the recorded ISO absent.

## Confidence & unknowns

Most claims are [DERIVED] — read and traced 2026-07-27 against the live tree,
not deliberately exercised. [VERIFIED] is reserved for the greps above and
runbook-documented field results.

**Unknown 1 — CLI failure exit semantics.** `vl.c:2884` passes `&error_fatal`
(set error ⇒ print and exit); the recorded field behavior
(`save-state-testing.md`) is qemu *staying alive* on a fresh boot. Both can't
be the whole story — either the failing sub-path returns without setting errp,
or the field failure took a differently-reporting path. F2's guidance holds
either way; the exact exit path is unresolved.

**Unknown 2 — `pgraph_vk_surface_flush`'s FIXME** ("should download all
surfaces to ram, but need to investigate corruption issue", `vk/surface.c:2370`):
non-draw-dirty surfaces are *not* downloaded at flush. Savevm is covered by
the pre-save dirty download + RAM being the source of everything else — but
whether surface content can be GPU-newer than RAM *without* `draw_dirty` set
is unproven. [ASSUMED: no such path exists.]

**Unknown 3 — restore with a missing/mismatched DVD** via monitor/CLI (no UI
dialog): IDE state restores claiming media; guest behavior on the next disc
read is untested.

**Unknown 4 — empty vertex-attr vmstate** (`nv2a.c:447`): VertexAttribute
shadows are not serialized; believed reconstructed from the serialized raw
regs + inline buffers on next use. A mid-draw savevm has never been attempted.

**Unknown 5 — cross-host portability** (same snapshot, different GPU/driver):
by I1/I3 nothing host-GPU-specific is in the stream, so it should load;
scale-factor/config interactions unverified. [ASSUMED]
