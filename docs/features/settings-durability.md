# Settings durability — re-spec

**Status: PROPOSED, not implemented.** Supersedes the interim periodic flush
(`xemu_settings_save_if_changed`, shipped in the same branch) — see §6.

**Origin:** a user reported that a controller remap "works right there" but is
never saved. Correct, and worse than it sounds. See [../../KNOWN_ISSUES.md](../../KNOWN_ISSUES.md) ISS-B17.

---

## 1. What is actually wrong

Settings reached disk from **exactly one place**:

```c
ui/xemu.c:1379:    atexit(xemu_settings_save);
```

No dirty flag, no autosave, no save-on-change anywhere. Two consequences:

1. **Everything lives in `g_config` until a clean exit.** A crash, a kill, or a
   power loss discards every change made that session. The remap "worked" because
   the UI mutates `g_config` in memory; nothing had touched disk.
2. **A failed write is silent.** `xemu_settings_save()` does
   `fprintf(stderr, "Failed to open config file for writing")` and returns. On a
   read-only config dir, a full disk, or a permissions problem, the user sees a
   setting take effect and never persist, with no indication why — *precisely the
   reported symptom, and it would survive fixing (1) alone.*

## 2. The requirement

> **Write to the file first. Apply the mutation only after the file has been
> written successfully.**

A setting is not "changed" until it is **durable**. If it cannot be persisted,
the user must see that immediately rather than discover it after a restart.

## 3. Why the literal form is not available

ImGui widgets bind **directly to the config struct**:

```cpp
ImGui::Combo("Backend", &g_config.display.renderer, ...)
ImGui::Checkbox("JIT Engine\n", &g_config.audio.use_dsp_jit)
```

**194 direct `g_config` references across `ui/xui/`.** The widget has already
mutated the struct by the time any save code could run — there is no interception
point to write-ahead *from*. Getting literal write-first would mean routing every
one of those through a setter API: a large, high-risk change touching every
settings surface, for a benefit the design below already delivers.

## 4. Proposed design — write-ahead with rollback

Keep the observable semantics of §2 without the rewrite.

1. Maintain a **durable snapshot**: the last TOML successfully written to disk
   (`s_last_written_toml` already exists and does this).
2. On a short cadence, render the current `g_config` to TOML and compare.
3. **Unchanged** → nothing happens.
4. **Changed** → attempt the write:
   - **Success** → the snapshot advances. The mutation is now durable and stands.
   - **Failure** → **restore `g_config` from the durable snapshot** and raise a
     visible, non-dismissable-until-acknowledged error naming the path and the
     `errno`.

From the user's seat this *is* §2: a change that cannot be persisted visibly
reverts and explains itself. The difference is that the revert happens within a
cadence window rather than within the widget callback.

**Write atomically** — render to `config.toml.tmp`, `fsync`, `rename()` over the
target. The current code `fopen("wb")`s the real file and writes into it, so an
interruption mid-write leaves a **truncated config**, which is worse than either
failure mode above. `rename()` on the same filesystem is atomic.

## 5. Open questions

| # | Question | Note |
|---|---|---|
| Q1 | **Cadence.** 2s (current interim) or on-blur/on-close of a settings pane? | Shorter = smaller rollback surprise window; longer = fewer renders. A render is a few KB of string building, so 2s is already free. |
| Q2 | **Rollback needs load-from-string.** `xemu_settings_load()` reads a path. | Either add a string variant, or write the snapshot to the temp file and load that. **This is the one piece of unknown work.** |
| Q3 | **Does rollback fight the UI?** Reverting `g_config` under a live ImGui pane may show a control snapping back. | That is arguably the *correct* feedback, but it should be paired with the error dialog so it does not look like a glitch. |
| Q4 | **Scope of rollback.** Revert everything, or only the delta that failed? | Everything is simpler and honest: the file is the unit of durability. Partial state has no meaning here. |
| Q6 | **Foreign-edit detection.** Refuse to overwrite a config changed on disk since our last write; surface a conflict. | Raised by C2 — hand-editing the TOML is the SBC remap workflow, so any cadence is a clobber window. |
| Q5 | Should the failure be **fatal to the settings UI** (disable further edits) until resolved? | Prevents a user stacking twenty changes against a read-only file. |

## 6. Relationship to what is currently in the branch

The interim already landed and is **strictly better than the previous state**,
but does not satisfy §2:

- ✅ Changes now survive a crash or kill (flushed every 2s).
- ❌ A failed write is still silent — no rollback, no dialog. `save_if_changed()`
  returns `false` on failure and **the caller ignores it**.
- ❌ Writes are still non-atomic; an interrupted write can truncate the config.

**The interim is a foundation, not a stopgap to be discarded:** the
generate/write split and the snapshot it introduced are exactly the pieces §4
needs. What is missing is failure handling and atomicity.

## 6b. Applying this to the Steel Battalion work

**Persistence needs no SBC-specific work.** `keyboard_sbc_scancode_map`
(`config_spec.yml:140`) and `display.ui.sbc_overlay` (`:528`) live in the same
config tree, so they are rendered by the same `generate_delta_toml()` and are
covered by §4 automatically.

Three SBC-specific constraints do fall out of it, and they are not obvious.

### C1 — Rollback must write INTO `g_config`, never replace it

`ui/xui/sbc-overlay.cc` caches **pointers into the config struct**:

```cpp
{ "Hatch",    &g_config.input.keyboard_sbc_scancode_map.cockpit_hatch, ... },
{ "Ignition", &g_config.input.keyboard_sbc_scancode_map.ignition,      ... },
```

`config_tree.store_to_struct(&g_config)` overwrites the existing struct in
place, so those pointers survive a reload — **which is what makes §4's rollback
viable at all.** If a future refactor ever swaps `g_config` for a fresh struct
or reallocates it, every one of those overlay entries dangles instantly. Treat
"restore writes in place" as a hard requirement of the rollback design, not an
implementation detail.

### C2 — SBC remapping *is* hand-editing the TOML

There is **no in-app rebind UI for the SBC keyboard map.** `input-manager.cc`
rebinding covers gamepads; the overlay only *reads* the map. So for the ~40
cockpit controls, the remap workflow is: edit `config.toml`, restart.

That inverts the risk. For a gamepad the danger is "my change was not saved";
for SBC it is **"xemu overwrote the file I was editing."** Any write cadence is
a clobber window, and it was exactly this that made the unseeded snapshot
(fixed) worse than the original exit-only save.

**Consequence for §4:** before writing, the design should check whether the file
changed on disk since our last write (mtime or a hash of the last-read content)
and refuse to overwrite a foreign edit — surfacing a conflict rather than
silently winning. Add as **Q6**.

### C3 — A rolled-back keybind is more dangerous here

Reverting a gamepad binding is a visible annoyance. Reverting an SBC keybind can
leave a user with a cockpit control that is not where they think it is, and there
are forty of them plus known collisions with global and VR hotkeys (ISS-B12).
Q3's "the control snaps back" feedback is therefore **mandatory** for SBC, not
cosmetic, and the error must name *which* binding reverted.

## 7. Suggested order

1. **Atomic write** (temp + `fsync` + `rename`). Small, self-contained, removes
   the truncation risk regardless of everything else.
2. **Surface write failure** in the UI. Turns a silent bug into a visible one —
   most of the user-facing benefit for a fraction of the work.
3. **Rollback on failure** (needs Q2). Completes §2.
4. Optionally, setter-routed writes for genuinely literal write-ahead — only if
   3 proves insufficient in practice.
