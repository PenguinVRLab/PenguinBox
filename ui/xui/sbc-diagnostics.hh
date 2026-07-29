// Copyright (C) 2026 Patrick Carey
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

#include "../xemu-sbc-device.h"
#include "../xemu-sim-profiles.h"

class SbcDiagnosticsWindow
{
public:
    bool m_is_open;

    SbcDiagnosticsWindow();

    void Draw();

private:
    enum { kAxisCount = 8 };
    enum { kJitterWindow = 48 };
    enum { kMinFaultSamples = 120 };

    struct AxisStat {
        int32_t min_v;
        int32_t max_v;
        int32_t ring[kJitterWindow];
        int ring_n;
        int ring_i;
        int32_t rest_spread;
    };

    AxisStat m_axis[kAxisCount];
    uint64_t m_buttons_ever;
    uint8_t m_toggles_ever;
    uint64_t m_samples;
    uint64_t m_last_seq;
    bool m_have_last_seq;

    uint8_t m_prev_raw[SBC_RAW_REPORT_MAX];
    uint64_t m_raw_changed_ms[SBC_RAW_REPORT_MAX];
    bool m_raw_ever_changed[SBC_RAW_REPORT_MAX];
    bool m_have_prev_raw;

    uint64_t m_rate_frames;
    uint64_t m_rate_ms;
    float m_fps;
    uint64_t m_last_new_frame_ms;
    uint64_t m_last_frames_received;

    char m_capture_path[1024];
    bool m_capture_path_init;
    bool m_capture_was_active;
    char m_capture_done_path[1024];
    uint64_t m_capture_done_count;
    uint64_t m_capture_peak_count;
    char m_replay_path[1024];

    bool m_pending_replay;
    char m_pending_replay_path[1024];
    bool m_pending_capture_path;
    char m_pending_capture_path_buf[1024];

    bool m_gate_was_active;
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
