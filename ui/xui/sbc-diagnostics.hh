//
// PenguinBox — Steel Battalion controller diagnostics panel
//
// Copyright (C) 2026 Patrick Carey
//
// SPDX-License-Identifier: MIT
//
// The read-only "show me, live, what the emulator thinks my hardware is doing"
// screen. Design of record: docs/features/sbc-zero-setup-plan.md §4 W1a (scope)
// and docs/features/sbc-analog-input.md §7 (what it answers / must show) + §10
// (pre-flight checklist, which this panel is the live half of).
//
// It exists FIRST, before the merge layer and the input reader, for one reason:
// today a Steel Battalion controller that cannot be opened fails SILENTLY, and
// that is the top community support problem. Every SbcDeviceStatus maps here to
// a specific instruction the user can act on without reading any code.
//
// DATA SOURCE: ui/xemu-sbc-device.h and nothing else. The panel never opens the
// device (design rule 1 — one owner of the handle) and never reads
// ControllerState/SteelBattalionState: the raw wire format is stable and
// verified, whereas the merged input state is being rewritten.
//
// WINDOW RECIPE — deliberately NOT the sbc-overlay one. sbc-overlay.cc is
// ImGuiWindowFlags_NoInputs / click-through / never-focused because the mouse
// IS the aiming lever and a HUD mirror must never steal it. This is a tool, not
// a HUD: it has buttons, checkboxes, a text field and scrolling regions, so it
// is an ordinary movable/resizable/focusable window. docs/internal/A4-sbc-
// overlay.md listed that boundary as undecided; this file decides it.
//
// OFF-STATE: with the device layer a no-op stub (libusb absent, feature off)
// the status stays SBC_DEV_ABSENT, no frames arrive, no fault hints fire and
// nothing is logged or notified. The window is only drawn when opened from the
// Machine menu; it is not persisted to config.
//
// GATING MODE (docs/features/sbc-zero-setup-plan.md §4 W1b, plus §10 of
// sbc-analog-input.md): when the sim gate recognises a title it pauses the VM
// and this SAME window becomes the pre-flight checklist — a verdict list, a
// Continue that is disabled while anything is blocking, a Re-scan, and a
// deliberately secondary Skip. The same ImGui window ID in a different role,
// never a second window (invariant 3), and the live gauges stay on because
// "ready the user has personally seen" is the whole point (§10 design notes).
// The gate is entered and left purely by polling sim_gate_active_profile();
// nothing outside this file pokes the panel.
//
#pragma once

#include <stdint.h>

#include "../xemu-sbc-device.h"
#include "../xemu-sim-profiles.h"

class SbcDiagnosticsWindow
{
public:
    bool m_is_open;

    SbcDiagnosticsWindow();

    /// Draw the panel. Called unconditionally every UI frame; returns
    /// immediately (after servicing deferred file-dialog results) when closed.
    void Draw();

private:
    enum { kAxisCount = 8 };
    enum { kJitterWindow = 48 };
    /* Fault hints stay silent until the session has this many frames, so a
     * freshly-opened panel never accuses working hardware. */
    enum { kMinFaultSamples = 120 };

    struct AxisStat {
        int32_t min_v;
        int32_t max_v;
        int32_t ring[kJitterWindow];
        int ring_n;
        int ring_i;
        int32_t rest_spread; // largest small-at-rest peak-to-peak seen
    };

    // --- session trackers (reset by the Reset button, never persisted) ---
    AxisStat m_axis[kAxisCount];
    uint64_t m_buttons_ever;
    uint8_t m_toggles_ever;
    uint64_t m_samples;
    uint64_t m_last_seq;
    bool m_have_last_seq;

    // Per-wire-byte activity: which bytes are moving now, and which have ever
    // moved. The second one is how the three undocumented fields (input byte 7,
    // the aiming-lever low bytes, the tuner's upper nibble) get investigated.
    uint8_t m_prev_raw[SBC_RAW_REPORT_MAX];
    uint64_t m_raw_changed_ms[SBC_RAW_REPORT_MAX];
    bool m_raw_ever_changed[SBC_RAW_REPORT_MAX];
    bool m_have_prev_raw;

    // --- frame rate / staleness ---
    uint64_t m_rate_frames;
    uint64_t m_rate_ms;
    float m_fps;
    uint64_t m_last_new_frame_ms;
    uint64_t m_last_frames_received;

    // --- capture / replay ---
    char m_capture_path[1024];
    bool m_capture_path_init;
    bool m_capture_was_active;
    char m_capture_done_path[1024];
    uint64_t m_capture_done_count;
    /* Highest count seen while recording. The contract does not say whether
     * sbc_device_capture_count() survives a stop, so the summary never relies
     * on reading it afterwards. */
    uint64_t m_capture_peak_count;
    char m_replay_path[1024];

    // File-dialog results land here and are applied on the next Draw, so every
    // sbc_device_* call stays on the UI thread inside the frame.
    bool m_pending_replay;
    char m_pending_replay_path[1024];
    bool m_pending_capture_path;
    char m_pending_capture_path_buf[1024];

    // --- gating mode ---
    // True on the frames the sim gate is holding the VM. Derived every frame
    // from sim_gate_active_profile(); never latched here, so the panel cannot
    // get stuck in a gating state the gate has already left.
    bool m_gate_was_active;
    // When the gate opened, in SDL ticks. "No reports are arriving" only
    // becomes blocking after this grace window, so a device that comes up a
    // few hundred ms after the pause never shows a red verdict it will
    // immediately contradict.
    uint64_t m_gate_open_ms;

    void ResetStats();
    void ServicePendingFileDialogs();
    void MakeDefaultCapturePath();
    void UpdateRate(const SbcDeviceState &st, uint64_t now_ms);
    void Observe(const SbcFrame &f, uint64_t now_ms);

    void DrawPreflightChecklist(const SimProfile *profile,
                                const SbcDeviceState &st, uint64_t now_ms);
    void DrawStatusBanner(const SbcDeviceState &st, const char *detail);
    void DrawLinkStats(const SbcDeviceState &st, uint64_t now_ms);
    void DrawSourceControls(const SbcDeviceState &st);
    void DrawCaptureControls(const SbcDeviceState &st);
    void DrawFaultHints();
    void DrawAxes(const SbcFrame &f, bool live);
    void DrawSwitches(const SbcFrame &f, bool live);
    void DrawButtons(const SbcFrame &f, bool live);
    void DrawRawReport(const SbcDeviceState &st, uint64_t now_ms);
    void CopyReportToClipboard(const SbcDeviceState &st, const char *detail);
};

extern SbcDiagnosticsWindow sbc_diagnostics_window;
