// Copyright (C) 2026 Patrick Carey
// SPDX-License-Identifier: MIT
#include "sbc-diagnostics.hh"
#include "common.hh"
#include "font-manager.hh"
#include "misc.hh"
#include "viewport-manager.hh"
#include "widgets.hh"

#include "../xemu-notifications.h"

#include <inttypes.h>
#include <string.h>
#include <time.h>

#include <string>

namespace
{

const ImVec4 kOn(0.35f, 1.00f, 0.45f, 1.00f);
const ImVec4 kOff(0.42f, 0.42f, 0.42f, 1.00f);
const ImVec4 kDim(0.72f, 0.72f, 0.72f, 1.00f);
const ImVec4 kWarn(1.00f, 0.72f, 0.25f, 1.00f);
const ImVec4 kBad(1.00f, 0.42f, 0.36f, 1.00f);
const ImVec4 kInfo(0.58f, 0.80f, 1.00f, 1.00f);

struct WireField {
    int first, last;
    const char *name;
    bool unknown;
};

const WireField kWireMap[] = {
    { 0, 0, "bReportId", false },
    { 1, 1, "bLength (expect 0x1A = 26)", false },
    { 2, 5, "dwButtons - button bits 0..31", false },
    { 6, 6, "bMoreButtons - bits 32..38 (toggle levels = mask 0x7C)", false },
    { 7, 8, "wPadding - UNDOCUMENTED (\"input byte 7\")", true },
    { 9, 9, "bAimingX (high byte)", false },
    { 10, 10, "bPadding - UNDOCUMENTED (aiming-lever low byte?)", true },
    { 11, 11, "bAimingY (high byte)", false },
    { 12, 13, "sRotationLever (s16)", false },
    { 14, 15, "sSightChangeX (s16)", false },
    { 16, 17, "sSightChangeY (s16)", false },
    { 18, 19, "wLeftPedal (u16, 0..0xFFFF)", false },
    { 20, 21, "wMiddlePedal (u16, 0..0xFFFF)", false },
    { 22, 23, "wRightPedal (u16, 0..0xFFFF)", false },
    { 24, 24, "ucTunerDial - low nibble 0..15; UPPER NIBBLE UNDOCUMENTED", true },
    { 25, 25, "ucGearLever (wire encoding)", false },
};

struct AxisDesc {
    const char *label;
    int32_t lo, hi;
    bool bipolar;
    const char *wire;
};

const AxisDesc kAxes[] = {
    { "Aiming X",  0,      65535, false, "b9 hi + ? lo"  },
    { "Aiming Y",  0,      65535, false, "b11 hi + ? lo" },
    { "Rotation",  -32768, 32767, true,  "b12-13"        },
    { "Sight X",   -32768, 32767, true,  "b14-15"        },
    { "Sight Y",   -32768, 32767, true,  "b16-17"        },
    { "Pedal L",   0,      65535, false, "b18-19"        },
    { "Pedal M",   0,      65535, false, "b20-21"        },
    { "Pedal R",   0,      65535, false, "b22-23"        },
};

int32_t AxisValue(const SbcFrame &f, int i)
{
    switch (i) {
    case 0: return (int32_t)f.aiming_x;
    case 1: return (int32_t)f.aiming_y;
    case 2: return (int32_t)f.rotation_lever;
    case 3: return (int32_t)f.sight_change_x;
    case 4: return (int32_t)f.sight_change_y;
    case 5: return (int32_t)f.left_pedal;
    case 6: return (int32_t)f.middle_pedal;
    case 7: return (int32_t)f.right_pedal;
    default: return 0;
    }
}

struct ButtonDesc {
    int bit;
    const char *label;
};

const ButtonDesc kButtons[] = {
    { 0,  "Main Weapon"     }, { 1,  "Sub Weapon"      },
    { 2,  "Lock On"         }, { 3,  "Eject"           },
    { 4,  "Cockpit Hatch"   }, { 5,  "Ignition"        },
    { 6,  "Start"           }, { 7,  "Open/Close"      },
    { 8,  "Map Zoom In/Out" }, { 9,  "Mode Select"     },
    { 10, "Sub Monitor"     }, { 11, "Zoom In"         },
    { 12, "Zoom Out"        }, { 13, "FSS"             },
    { 14, "Manipulator"     }, { 15, "Line Color"      },
    { 16, "Washing"         }, { 17, "Extinguisher"    },
    { 18, "Chaff"           }, { 19, "Tank Detach"     },
    { 20, "Override"        }, { 21, "Night Scope"     },
    { 22, "Func 1"          }, { 23, "Func 2"          },
    { 24, "Func 3"          }, { 25, "Main Wpn Ctl"    },
    { 26, "Sub Wpn Ctl"     }, { 27, "Magazine Change" },
    { 28, "COM 1"           }, { 29, "COM 2"           },
    { 30, "COM 3"           }, { 31, "COM 4"           },
    { 32, "COM 5"           }, { 33, "Sight Change"    },
    { 34, "FILT (toggle)"   }, { 35, "OXY (toggle)"    },
    { 36, "FUEL (toggle)"   }, { 37, "BUFFER (toggle)" },
    { 38, "VT LOC (toggle)" },
};

struct ToggleDesc {
    uint8_t mask;
    int press_bit;
    const char *label;
};

const ToggleDesc kToggles[] = {
    { 0x04, 34, "FILT Control System"    },
    { 0x08, 35, "Oxygen Supply System"   },
    { 0x10, 36, "Fuel Flow Rate"         },
    { 0x20, 37, "Buffer Material"        },
    { 0x40, 38, "VT Location Measurement" },
};

struct GearDecode {
    const char *glyph;
    const char *enc;
    bool reverse;
};

GearDecode DecodeGear(uint8_t g)
{
    switch (g) {
    case 1:   return { "1", "1-5",   false };
    case 2:   return { "2", "1-5",   false };
    case 3:   return { "3", "1-5",   false };
    case 4:   return { "4", "1-5",   false };
    case 5:   return { "5", "1-5",   false };
    case 7:   return { "R", "7-15",  true  };
    case 8:   return { "N", "7-15",  false };
    case 9:   return { "1", "7-15",  false };
    case 10:  return { "2", "7-15",  false };
    case 11:  return { "3", "7-15",  false };
    case 12:  return { "4", "7-15",  false };
    case 13:  return { "5", "7-15",  false };
    case 15:  return { "R", "7-15",  true  };
    case 254: return { "R", "xemu",  true  };
    case 255: return { "N", "xemu",  false };
    default:  return { "?", "unknown", false };
    }
}

const char *StatusName(SbcDeviceStatus s)
{
    switch (s) {
    case SBC_DEV_ABSENT:        return "ABSENT";
    case SBC_DEV_READY:         return "READY";
    case SBC_DEV_NO_PERMISSION: return "NO_PERMISSION";
    case SBC_DEV_NO_DRIVER:     return "NO_DRIVER";
    case SBC_DEV_BUSY:          return "BUSY";
    case SBC_DEV_ERROR:         return "ERROR";
    default:                    return "?";
    }
}

const char *kUdevRulePath = "/etc/udev/rules.d/60-penguinbox-steel-battalion.rules";
const char *kUdevRuleText =
    "SUBSYSTEM==\"usb\", ATTR{idVendor}==\"0a7b\", ATTR{idProduct}==\"d000\", "
    "TAG+=\"uaccess\", MODE=\"0660\", GROUP=\"plugdev\"";
const char *kUdevInstallCmd =
    "sudo tee /etc/udev/rules.d/60-penguinbox-steel-battalion.rules >/dev/null <<'EOF'\n"
    "SUBSYSTEM==\"usb\", ATTR{idVendor}==\"0a7b\", ATTR{idProduct}==\"d000\", "
    "TAG+=\"uaccess\", MODE=\"0660\", GROUP=\"plugdev\"\n"
    "EOF\n"
    "sudo udevadm control --reload-rules && sudo udevadm trigger";

void CopyButton(const char *id, const char *text, const char *label = "Copy")
{
    ImGui::PushID(id);
    if (ImGui::Button(label)) {
        ImGui::SetClipboardText(text);
        xemu_queue_notification("Copied to clipboard");
    }
    ImGui::PopID();
}

void ReadOnlyBlock(const char *id, const char *text, int lines)
{
    static char buf[2048];
    snprintf(buf, sizeof(buf), "%s", text);
    ImGui::PushFont(g_font_mgr.m_fixed_width_font);
    ImGui::InputTextMultiline(
        id, buf, sizeof(buf),
        ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * lines +
                             ImGui::GetStyle().FramePadding.y * 2.0f),
        ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo);
    ImGui::PopFont();
}

enum VerdictLevel {
    kVerdictReady = 0,
    kVerdictWarn,
    kVerdictBlock,
};

void VerdictRow(int level, const char *label, const char *detail)
{
    const char *tag;
    ImVec4 col;

    switch (level) {
    case kVerdictReady: tag = "[ OK ]"; col = kOn;   break;
    case kVerdictWarn:  tag = "[WARN]"; col = kWarn; break;
    default:            tag = "[STOP]"; col = kBad;  break;
    }

    ImGui::PushFont(g_font_mgr.m_fixed_width_font);
    ImGui::TextColored(col, "%s", tag);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(col, "%s", label);

    if (detail != NULL && detail[0]) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextWrapped("%s", detail);
        ImGui::PopStyleColor();
        ImGui::Unindent();
    }
}

}

SbcDiagnosticsWindow sbc_diagnostics_window;

SbcDiagnosticsWindow::SbcDiagnosticsWindow()
    : m_is_open(false)
{
    m_capture_path[0] = '\0';
    m_capture_path_init = false;
    m_capture_was_active = false;
    m_capture_done_path[0] = '\0';
    m_capture_done_count = 0;
    m_capture_peak_count = 0;
    m_replay_path[0] = '\0';
    m_pending_replay = false;
    m_pending_replay_path[0] = '\0';
    m_pending_capture_path = false;
    m_pending_capture_path_buf[0] = '\0';
    m_rate_frames = 0;
    m_rate_ms = 0;
    m_fps = 0.0f;
    m_last_new_frame_ms = 0;
    m_last_frames_received = 0;
    m_gate_was_active = false;
    m_gate_open_ms = 0;
    ResetStats();
}

void SbcDiagnosticsWindow::ResetStats()
{
    for (int i = 0; i < kAxisCount; i++) {
        AxisStat &a = m_axis[i];
        a.min_v = 0;
        a.max_v = 0;
        a.ring_n = 0;
        a.ring_i = 0;
        a.rest_spread = 0;
        memset(a.ring, 0, sizeof(a.ring));
    }
    m_buttons_ever = 0;
    m_toggles_ever = 0;
    m_samples = 0;
    m_last_seq = 0;
    m_have_last_seq = false;
    memset(m_prev_raw, 0, sizeof(m_prev_raw));
    memset(m_raw_changed_ms, 0, sizeof(m_raw_changed_ms));
    memset(m_raw_ever_changed, 0, sizeof(m_raw_ever_changed));
    m_have_prev_raw = false;
}

void SbcDiagnosticsWindow::MakeDefaultCapturePath()
{
    if (m_capture_path_init) {
        return;
    }
    m_capture_path_init = true;

    char stamp[32];
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    struct tm *lt = localtime(&t);
    if (lt) {
        tmv = *lt;
    } else {
        memset(&tmv, 0, sizeof(tmv));
    }
#else
    if (!localtime_r(&t, &tmv)) {
        memset(&tmv, 0, sizeof(tmv));
    }
#endif
    if (strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv) == 0) {
        snprintf(stamp, sizeof(stamp), "capture");
    }

    const char *base = xemu_settings_get_base_path();
    snprintf(m_capture_path, sizeof(m_capture_path), "%s%ssbc-capture-%s.bin",
             base ? base : ".", (base && base[0]) ? "/" : "", stamp);
}

void SbcDiagnosticsWindow::ServicePendingFileDialogs()
{
    if (m_pending_capture_path) {
        m_pending_capture_path = false;
        snprintf(m_capture_path, sizeof(m_capture_path), "%s",
                 m_pending_capture_path_buf);
        m_capture_path_init = true;
    }

    if (m_pending_replay) {
        m_pending_replay = false;
        if (sbc_device_replay_file(m_pending_replay_path)) {
            snprintf(m_replay_path, sizeof(m_replay_path), "%s",
                     m_pending_replay_path);
            ResetStats();
            xemu_queue_notification("SBC: replaying captured report stream");
        } else {
            m_replay_path[0] = '\0';
            xemu_queue_error_message(
                "SBC: could not open that replay file. It must be a capture "
                "written by this panel's Record button.");
        }
    }
}

void SbcDiagnosticsWindow::UpdateRate(const SbcDeviceState &st, uint64_t now_ms)
{
    if (m_rate_ms == 0) {
        m_rate_ms = now_ms;
        m_rate_frames = st.frames_received;
    }
    if (st.frames_received != m_last_frames_received) {
        m_last_frames_received = st.frames_received;
        m_last_new_frame_ms = now_ms;
    }
    const uint64_t elapsed = now_ms - m_rate_ms;
    if (elapsed >= 500) {
        const uint64_t delta = (st.frames_received >= m_rate_frames) ?
                                   (st.frames_received - m_rate_frames) :
                                   0;
        m_fps = (float)delta * 1000.0f / (float)elapsed;
        m_rate_ms = now_ms;
        m_rate_frames = st.frames_received;
    }
}

void SbcDiagnosticsWindow::Observe(const SbcFrame &f, uint64_t now_ms)
{
    const bool first = (m_samples == 0);
    m_samples++;

    for (int i = 0; i < kAxisCount; i++) {
        const int32_t v = AxisValue(f, i);
        AxisStat &a = m_axis[i];
        if (first) {
            a.min_v = a.max_v = v;
        } else {
            if (v < a.min_v) a.min_v = v;
            if (v > a.max_v) a.max_v = v;
        }

        a.ring[a.ring_i] = v;
        a.ring_i = (a.ring_i + 1) % kJitterWindow;
        if (a.ring_n < kJitterWindow) {
            a.ring_n++;
        }
        if (a.ring_n == kJitterWindow) {
            int32_t lo = a.ring[0], hi = a.ring[0];
            for (int k = 1; k < kJitterWindow; k++) {
                if (a.ring[k] < lo) lo = a.ring[k];
                if (a.ring[k] > hi) hi = a.ring[k];
            }
            const int32_t spread = hi - lo;
            const int32_t span = kAxes[i].hi - kAxes[i].lo;
            if (spread > span / 128 && spread <= span / 16 &&
                spread > a.rest_spread) {
                a.rest_spread = spread;
            }
        }
    }

    m_buttons_ever |= f.buttons;
    m_toggles_ever |= f.toggle_levels;

    int n = f.raw_len;
    if (n > SBC_RAW_REPORT_MAX) {
        n = SBC_RAW_REPORT_MAX;
    }
    if (m_have_prev_raw) {
        for (int i = 0; i < n; i++) {
            if (f.raw[i] != m_prev_raw[i]) {
                m_raw_changed_ms[i] = now_ms;
                m_raw_ever_changed[i] = true;
            }
        }
    }
    memcpy(m_prev_raw, f.raw, SBC_RAW_REPORT_MAX);
    m_have_prev_raw = true;
}

void SbcDiagnosticsWindow::DrawPreflightChecklist(const SimProfile *profile,
                                                  const SbcDeviceState &st,
                                                  uint64_t now_ms)
{
    const bool synthetic = sbc_device_synthetic_enabled();
    const bool have_source = sim_gate_device_layer_up() || synthetic;
    bool blocking = false;

    ImGui::TextWrapped(
        "%s expects specific hardware, so PenguinBox has paused the boot for a "
        "pre-flight check. Everything below is live: work each control and "
        "watch it move. Finding a dead pedal here costs five seconds; finding "
        "it mid-mission costs the sortie.",
        profile->display_name);
    ImGui::PushFont(g_font_mgr.m_fixed_width_font);
    ImGui::TextColored(kOff, "XBE title ID 0x%08X",
                       (unsigned)profile->title_id);
    ImGui::PopFont();
    if (profile->notes != NULL && profile->notes[0]) {
        ImGui::TextColored(kOff, "%s", profile->notes);
    }
    ImGui::Spacing();

    if (!have_source) {
        VerdictRow(kVerdictReady, "No device layer - nothing to check",
                   "The host-side controller reader is not enabled in this "
                   "session (or this build has no libusb), so there is no "
                   "hardware state to verify. Press Continue: the game boots "
                   "exactly as it would if this check did not exist.");
    } else {
        for (unsigned i = 0; i < profile->num_requirements; i++) {
            const SimRequirement &r = profile->requirements[i];
            const char *detail = "";
            char label[224];
            int level;

            switch (r.device) {
            case SIM_DEV_SBC:
                switch (st.status) {
                case SBC_DEV_READY:
                    level = kVerdictReady;
                    detail = synthetic ?
                        "Reporting - but from the synthetic/replay source, "
                        "not from your controller." :
                        "Open and reporting.";
                    break;
                case SBC_DEV_ABSENT:
                    level = r.required ? kVerdictBlock : kVerdictWarn;
                    detail = "Not on the bus. Plug it in - this list updates "
                             "live - or press Re-scan.";
                    break;
                default:
                    level = r.required ? kVerdictBlock : kVerdictWarn;
                    detail = "Detected, but PenguinBox cannot open it. The "
                             "banner below says exactly what to do.";
                    break;
                }
                break;
            default:
                level = kVerdictWarn;
                detail = "This build does not know how to check that device.";
                break;
            }

            snprintf(label, sizeof(label), "%s (%s)", r.label,
                     r.required ? "required" : "optional");
            if (level == kVerdictBlock) {
                blocking = true;
            }
            VerdictRow(level, label, detail);
        }

        if (st.status == SBC_DEV_READY) {
            char d[224];

            if (st.frames_received == 0) {
                if (now_ms - m_gate_open_ms > 1500) {
                    blocking = true;
                    VerdictRow(kVerdictBlock, "Reports arriving",
                               "The device is open but has not sent a single "
                               "report. Nothing you do with the controller can "
                               "reach the game in this state.");
                } else {
                    VerdictRow(kVerdictWarn, "Reports arriving",
                               "Waiting for the first report...");
                }
            } else if (now_ms - m_last_new_frame_ms > 2000) {
                blocking = true;
                snprintf(d, sizeof(d),
                         "Stopped reporting - no new frame for %.1f s. Replug "
                         "it, then press Re-scan.",
                         (double)(now_ms - m_last_new_frame_ms) / 1000.0);
                VerdictRow(kVerdictBlock, "Reports arriving", d);
            } else {
                snprintf(d, sizeof(d), "%" PRIu64 " reports, %.1f/s.",
                         st.frames_received, (double)m_fps);
                VerdictRow(kVerdictReady, "Reports arriving", d);
            }
        }

        if (st.status == SBC_DEV_READY && st.frames_received > 0) {
            if (m_samples < (uint64_t)kMinFaultSamples) {
                char d[224];
                snprintf(d, sizeof(d),
                         "Collecting samples (%" PRIu64 "/%d). Work each pedal "
                         "and lever to its stops so this has something to "
                         "judge.",
                         m_samples, (int)kMinFaultSamples);
                VerdictRow(kVerdictWarn, "Axis travel", d);
            } else {
                int faults = 0;

                for (int i = 0; i < kAxisCount; i++) {
                    const AxisDesc &ad = kAxes[i];
                    const AxisStat &as = m_axis[i];
                    const int32_t span = ad.hi - ad.lo;
                    char msg[240];
                    int level = -1;

                    if (as.min_v >= ad.hi - span / 64) {
                        level = kVerdictBlock;
                        snprintf(msg, sizeof(msg),
                                 "%s: PINNED AT MAXIMUM (never left 0x%04X) - "
                                 "unbound, disconnected or reading full scale.",
                                 ad.label, (unsigned)(uint16_t)as.max_v);
                    } else if (ad.bipolar && as.max_v <= ad.lo + span / 64) {
                        level = kVerdictBlock;
                        snprintf(msg, sizeof(msg),
                                 "%s: PINNED AT MINIMUM (never left 0x%04X).",
                                 ad.label, (unsigned)(uint16_t)as.min_v);
                    } else if (as.max_v - as.min_v > span / 8 &&
                               as.max_v < ad.hi - span / 10) {
                        level = kVerdictBlock;
                        snprintf(msg, sizeof(msg),
                                 "%s: never reaches full travel (best 0x%04X "
                                 "of 0x%04X) - push it to the stop.",
                                 ad.label, (unsigned)(uint16_t)as.max_v,
                                 (unsigned)(uint16_t)ad.hi);
                    } else if (as.max_v == as.min_v) {
                        level = kVerdictWarn;
                        snprintf(msg, sizeof(msg),
                                 "%s: has not moved this session (constant "
                                 "0x%04X) - exercise it to confirm it works.",
                                 ad.label, (unsigned)(uint16_t)as.min_v);
                    } else if (as.rest_spread > 0) {
                        level = kVerdictWarn;
                        snprintf(msg, sizeof(msg),
                                 "%s: jittering at rest (+/-%d counts) - dirty "
                                 "or worn potentiometer.",
                                 ad.label, (int)as.rest_spread);
                    }

                    if (level >= 0) {
                        faults++;
                        if (level == kVerdictBlock) {
                            blocking = true;
                        }
                        VerdictRow(level, msg, "");
                    }
                }

                if (faults == 0) {
                    VerdictRow(kVerdictReady, "Axis travel",
                               "All eight axes move, reach their stops and sit "
                               "still at rest.");
                }
            }
        }

        if (st.status == SBC_DEV_READY &&
            m_samples >= (uint64_t)kMinFaultSamples) {
            const int total = (int)(sizeof(kButtons) / sizeof(kButtons[0]));
            int never = 0;

            for (int i = 0; i < total; i++) {
                if (!(m_buttons_ever & (1ULL << kButtons[i].bit))) {
                    never++;
                }
            }
            if (never > 0) {
                char d[224];
                snprintf(d, sizeof(d),
                         "%d of %d have not fired this session. Press each one "
                         "and watch it light up below; anything still dark is "
                         "unbound, dead or oxidised.",
                         never, total);
                VerdictRow(kVerdictWarn, "Buttons and switches", d);
            } else {
                VerdictRow(kVerdictReady, "Buttons and switches",
                           "Every button and switch has fired at least once.");
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::BeginDisabled(blocking);
    const bool go = ImGui::Button("Continue",
                                  g_viewport_mgr.Scale(ImVec2(160, 0)));
    if (!blocking) {
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Re-scan")) {
        ResetStats();
        m_gate_open_ms = now_ms;
    }
    ImGui::SameLine();
    HelpMarker("Throws away everything this panel has observed and judges the "
               "hardware again from scratch. Plugging the controller in or out "
               "needs no button: the device layer watches the bus "
               "continuously.");

    ImGui::SameLine(0.0f, 32.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.22f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    const bool skip = ImGui::Button("Skip anyway");
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::TextColored(kOff, "boots the game as it is");

    if (blocking) {
        ImGui::TextColored(kBad,
                           "Something above is blocking. Fix it and this "
                           "unlocks by itself - or Skip, if you would rather "
                           "play with it broken.");
    } else {
        ImGui::TextColored(kDim,
                           "Nothing is blocking. Press Continue to boot.");
    }
    ImGui::TextColored(kOff,
                       "Do not want this check? Set [input] "
                       "sim_preflight_gate = false in xemu.toml.");

    if (go || skip) {
        sim_gate_release(skip);
        m_is_open = false;
    }
}

void SbcDiagnosticsWindow::DrawStatusBanner(const SbcDeviceState &st,
                                            const char *detail)
{
    ImVec4 bg, fg;
    const char *headline;

    switch (st.status) {
    case SBC_DEV_READY:
        bg = ImVec4(0.10f, 0.26f, 0.12f, 1.00f); fg = kOn;
        headline = "CONTROLLER CONNECTED";
        break;
    case SBC_DEV_ABSENT:
        bg = ImVec4(0.16f, 0.16f, 0.18f, 1.00f); fg = kDim;
        headline = "NO CONTROLLER DETECTED";
        break;
    case SBC_DEV_NO_PERMISSION:
    case SBC_DEV_NO_DRIVER:
    case SBC_DEV_BUSY:
        bg = ImVec4(0.30f, 0.22f, 0.05f, 1.00f); fg = kWarn;
        headline = (st.status == SBC_DEV_NO_PERMISSION) ?
                       "DETECTED, BUT PENGUINBOX CANNOT OPEN IT" :
                   (st.status == SBC_DEV_NO_DRIVER) ?
                       "DETECTED, BUT NOT BOUND TO A USABLE DRIVER" :
                       "DETECTED, BUT ANOTHER PROGRAM IS HOLDING IT";
        break;
    case SBC_DEV_ERROR:
    default:
        bg = ImVec4(0.32f, 0.12f, 0.11f, 1.00f); fg = kBad;
        headline = "DEVICE ERROR";
        break;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::BeginChild("##sbc_status", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

    ImGui::TextColored(fg, "%s", headline);
    ImGui::Separator();

    switch (st.status) {
    case SBC_DEV_READY:
        ImGui::TextWrapped(
            "Steel Battalion controller %04x:%04x is open and reporting. "
            "Move a control and watch the gauges below.",
            (unsigned)SBC_USB_VENDOR_ID, (unsigned)SBC_USB_PRODUCT_ID);
        ImGui::Text("%.1f frames/sec", (double)m_fps);
        if (detail[0]) {
            ImGui::SameLine();
            ImGui::TextColored(kDim, "  |  %s", detail);
        }
        break;

    case SBC_DEV_ABSENT:
        ImGui::TextWrapped(
            "No Steel Battalion controller (USB %04x:%04x) is on the bus. "
            "Connect it now - this panel updates live, so plug it in and watch "
            "this banner change. If it is already plugged in, try another port "
            "or cable, then reopen this panel.",
            (unsigned)SBC_USB_VENDOR_ID, (unsigned)SBC_USB_PRODUCT_ID);
        ImGui::TextWrapped(
            "You do not need hardware to exercise this panel: turn on "
            "\"Synthetic input\" below and every gauge will move. This is also "
            "what you will see on a build without host-controller support.");
        break;

    case SBC_DEV_NO_PERMISSION:
        ImGui::TextWrapped(
            "The controller is on the bus but this user account is not allowed "
            "to open it. On Linux that is a one-time udev rule. Install it, "
            "then unplug and replug the controller - no reboot, no restart.");
        ImGui::Spacing();
        ImGui::TextColored(kDim, "Rule file: %s", kUdevRulePath);
        ReadOnlyBlock("##udevrule", kUdevRuleText, 3);
        CopyButton("cp_rule", kUdevRuleText, "Copy rule");
        ImGui::SameLine();
        CopyButton("cp_cmd", kUdevInstallCmd, "Copy install command");
        ImGui::TextWrapped(
            "Both halves matter: TAG+=\"uaccess\" covers a normal desktop "
            "login, and MODE/GROUP covers a session with no seat (SSH), where "
            "uaccess silently does nothing.");
        break;

    case SBC_DEV_NO_DRIVER:
        ImGui::TextWrapped(
            "The controller is on the bus but Windows has not bound it to "
            "WinUSB, so it cannot be opened. Run Zadig once, as administrator:");
        ImGui::Bullet();
        ImGui::TextWrapped("Options > List All Devices.");
        ImGui::Bullet();
        ImGui::TextWrapped(
            "Pick the Steel Battalion controller (USB ID %04x:%04x).",
            (unsigned)SBC_USB_VENDOR_ID, (unsigned)SBC_USB_PRODUCT_ID);
        ImGui::Bullet();
        ImGui::TextWrapped(
            "Choose WinUSB as the target driver and press Replace/Install "
            "Driver, then replug the controller.");
        ImGui::TextWrapped(
            "Nothing is being taken away from another program: this device "
            "class has no in-box Windows driver.");
        if (ImGui::Button("Open zadig.akeo.ie")) {
            SDL_OpenURL("https://zadig.akeo.ie/");
        }
        ImGui::SameLine();
        CopyButton("cp_zadig", "https://zadig.akeo.ie/", "Copy link");
        break;

    case SBC_DEV_BUSY:
        ImGui::TextWrapped(
            "The controller is on the bus but another process is holding its "
            "interface. Close any other emulator, controller tool or second "
            "copy of PenguinBox, then unplug and replug the controller. On "
            "Linux the kernel's xpad driver also claims this device; it is "
            "detached automatically, so a persistent BUSY usually means a "
            "second program, not the kernel.");
        break;

    case SBC_DEV_ERROR:
    default:
        ImGui::TextWrapped(
            "The controller was found but could not be brought up. The device "
            "layer reported:");
        ImGui::Spacing();
        ImGui::PushFont(g_font_mgr.m_fixed_width_font);
        ImGui::TextWrapped("%s", detail[0] ? detail : "(no detail reported)");
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Please send that line with a bug report - use \"Copy support "
            "report\" at the bottom of this panel.");
        break;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void SbcDiagnosticsWindow::DrawLinkStats(const SbcDeviceState &st,
                                         uint64_t now_ms)
{
    ImGui::Text("Status");
    ImGui::SameLine();
    ImGui::TextColored(st.status == SBC_DEV_READY ? kOn : kWarn, "%s",
                       StatusName(st.status));
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextColored(kDim, "Frames");
    ImGui::SameLine();
    ImGui::Text("%" PRIu64, st.frames_received);
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextColored(kDim, "Rate");
    ImGui::SameLine();
    ImGui::Text("%.1f/s", (double)m_fps);
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextColored(kDim, "Seq");
    ImGui::SameLine();
    ImGui::Text("%" PRIu64, st.frames_received ? st.frame.seq : 0);

    ImGui::TextColored(kDim, "Transfer errors");
    ImGui::SameLine();
    ImGui::TextColored(st.transfer_errors ? kWarn : kDim, "%" PRIu64,
                       st.transfer_errors);
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextColored(kDim, "Malformed reports");
    ImGui::SameLine();
    ImGui::TextColored(st.malformed_reports ? kWarn : kDim, "%" PRIu64,
                       st.malformed_reports);
    if (st.malformed_reports) {
        ImGui::SameLine();
        ImGui::TextColored(kWarn, "(short or over-long reads - expected %d bytes)",
                           SBC_RAW_REPORT_EXPECTED);
    }

    if (st.frames_received > 0) {
        const uint64_t age = now_ms - m_last_new_frame_ms;
        if (age > 1000) {
            ImGui::TextColored(kWarn,
                               "No new frame for %.1f s - the device has "
                               "stopped reporting.",
                               (double)age / 1000.0);
        }
    }
}

void SbcDiagnosticsWindow::DrawSourceControls(const SbcDeviceState &st)
{
    (void)st;

    bool synthetic = sbc_device_synthetic_enabled();
    if (ImGui::Checkbox("Synthetic input (no hardware required)", &synthetic)) {
        sbc_device_set_synthetic(synthetic);
        ResetStats();
    }
    ImGui::SameLine();
    HelpMarker("Publishes generated frames instead of touching USB. Every "
               "gauge, lamp and raw byte below must move - that is the check "
               "that the panel itself works.");

    ImGui::SameLine(0.0f, 16.0f);
    if (ImGui::Button("Load replay file...")) {
        static const SDL_DialogFileFilter filters[] = {
            { "SBC capture (*.bin)", "bin" },
            { "All Files", "*" },
        };
        ShowOpenFileDialog(filters, 2, m_capture_path, [this](const char *path) {
            if (!path) {
                return;
            }
            snprintf(m_pending_replay_path, sizeof(m_pending_replay_path), "%s",
                     path);
            m_pending_replay = true;
        });
    }
    ImGui::SameLine();
    HelpMarker("Replays a raw report stream recorded by the Record button "
               "below, byte for byte. This is how a capture from someone "
               "else's real controller is inspected here.");

    ImGui::TextColored(kDim, "Source:");
    ImGui::SameLine();
    if (m_replay_path[0] && synthetic) {
        ImGui::TextColored(kInfo, "replay - %s", m_replay_path);
    } else if (synthetic) {
        ImGui::TextColored(kInfo, "synthetic generator");
    } else {
        ImGui::TextColored(kDim, "real device (host USB)");
    }
}

void SbcDiagnosticsWindow::DrawCaptureControls(const SbcDeviceState &st)
{
    MakeDefaultCapturePath();

    const bool active = sbc_device_capture_active();
    const uint64_t count = sbc_device_capture_count();

    if (active && count > m_capture_peak_count) {
        m_capture_peak_count = count;
    }
    if (m_capture_was_active && !active) {
        m_capture_done_count = (count > m_capture_peak_count) ?
                                   count : m_capture_peak_count;
        snprintf(m_capture_done_path, sizeof(m_capture_done_path), "%s",
                 m_capture_path);
    }
    m_capture_was_active = active;

    ImGui::TextWrapped(
        "Record the raw report stream to a file and send it to us. Exercise "
        "everything while recording: each pedal to the floor, both sticks to "
        "their stops, the rotation lever both ways, every switch and button, "
        "the gear lever through all positions and the tuner through all 16. "
        "That file is the ground truth we write decode tests against.");
    ImGui::Spacing();

    ImGui::BeginDisabled(active);
    ImGui::SetNextItemWidth(-g_viewport_mgr.Scale(ImVec2(120, 0)).x);
    ImGui::InputText("##capture_path", m_capture_path, sizeof(m_capture_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        static const SDL_DialogFileFilter filters[] = {
            { "SBC capture (*.bin)", "bin" },
            { "All Files", "*" },
        };
        ShowSaveFileDialog(filters, 2, m_capture_path, [this](const char *path) {
            if (!path) {
                return;
            }
            snprintf(m_pending_capture_path_buf,
                     sizeof(m_pending_capture_path_buf), "%s", path);
            m_pending_capture_path = true;
        });
    }
    ImGui::EndDisabled();

    if (!active) {
        if (ImGui::Button("Start recording")) {
            if (sbc_device_capture_start(m_capture_path)) {
                m_capture_done_path[0] = '\0';
                m_capture_done_count = 0;
                m_capture_peak_count = 0;
                xemu_queue_notification("SBC: recording report stream");
            } else {
                xemu_queue_error_message(
                    "SBC: could not open that file for writing. Pick another "
                    "location with Browse.");
            }
        }
        if (st.status != SBC_DEV_READY && !sbc_device_synthetic_enabled()) {
            ImGui::SameLine();
            ImGui::TextColored(kWarn,
                               "No frames are arriving - a capture started now "
                               "would be empty.");
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.16f, 0.14f, 1.0f));
        const bool stop = ImGui::Button("Stop recording");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextColored(kOn, "RECORDING - %" PRIu64 " frames", count);
        if (stop) {
            sbc_device_capture_stop();
            const uint64_t after = sbc_device_capture_count();
            m_capture_done_count = (after > m_capture_peak_count) ?
                                       after : m_capture_peak_count;
            snprintf(m_capture_done_path, sizeof(m_capture_done_path), "%s",
                     m_capture_path);
            m_capture_was_active = false;
            char msg[1152];
            snprintf(msg, sizeof(msg), "SBC: wrote %" PRIu64 " frames to %s",
                     m_capture_done_count, m_capture_done_path);
            xemu_queue_notification(msg);
        }
    }

    if (!active && m_capture_done_path[0]) {
        ImGui::TextColored(kOn, "Wrote %" PRIu64 " frames to:",
                           m_capture_done_count);
        ImGui::TextWrapped("%s", m_capture_done_path);
        CopyButton("cp_capture_path", m_capture_done_path, "Copy file path");
        if (m_capture_done_count == 0) {
            ImGui::SameLine();
            ImGui::TextColored(kWarn,
                               "Empty capture - no frames arrived while "
                               "recording.");
        }
    }
}

void SbcDiagnosticsWindow::DrawFaultHints()
{
    if (m_samples < (uint64_t)kMinFaultSamples) {
        ImGui::TextColored(kDim,
                           "Collecting samples (%" PRIu64 "/%d) - fault hints "
                           "stay quiet until there is enough data.",
                           m_samples, (int)kMinFaultSamples);
        return;
    }

    int shown = 0;
    for (int i = 0; i < kAxisCount; i++) {
        const AxisDesc &d = kAxes[i];
        const AxisStat &a = m_axis[i];
        const int32_t span = d.hi - d.lo;

        if (a.min_v >= d.hi - span / 64) {
            ImGui::TextColored(kBad,
                               "%s: PINNED AT MAXIMUM (never left 0x%04X). "
                               "This is the default-to-max trap - the axis is "
                               "unbound, disconnected or reading full-scale.",
                               d.label, (unsigned)(uint16_t)a.max_v);
            shown++;
            continue;
        }
        if (d.bipolar && a.max_v <= d.lo + span / 64) {
            ImGui::TextColored(kBad,
                               "%s: PINNED AT MINIMUM (never left 0x%04X).",
                               d.label, (unsigned)(uint16_t)a.min_v);
            shown++;
            continue;
        }
        if (a.max_v == a.min_v) {
            ImGui::TextColored(kWarn,
                               "%s: never moved (constant 0x%04X) - unbound, "
                               "dead pot, or simply not exercised yet.",
                               d.label, (unsigned)(uint16_t)a.min_v);
            shown++;
            continue;
        }
        if (a.max_v - a.min_v > span / 8) {
            if (a.max_v < d.hi - span / 10) {
                ImGui::TextColored(kWarn,
                                   "%s: never reached full travel (best "
                                   "0x%04X of 0x%04X) - needs calibration, or "
                                   "a worn pot.",
                                   d.label, (unsigned)(uint16_t)a.max_v,
                                   (unsigned)(uint16_t)d.hi);
                shown++;
            }
            if (d.bipolar && a.min_v > d.lo + span / 10) {
                ImGui::TextColored(kWarn,
                                   "%s: never reached full travel in the "
                                   "other direction (best 0x%04X).",
                                   d.label, (unsigned)(uint16_t)a.min_v);
                shown++;
            }
        }
        if (a.rest_spread > 0) {
            ImGui::TextColored(kWarn,
                               "%s: jittering at rest (+/-%d counts) - dirty "
                               "or worn potentiometer.",
                               d.label, (int)a.rest_spread);
            shown++;
        }
    }

    int never = 0;
    for (size_t i = 0; i < sizeof(kButtons) / sizeof(kButtons[0]); i++) {
        if (!(m_buttons_ever & (1ULL << kButtons[i].bit))) {
            never++;
        }
    }
    if (never > 0) {
        ImGui::TextColored(kDim,
                           "%d of %d buttons have not fired this session. "
                           "Press each one; anything still dark below is "
                           "unbound, dead or oxidised.",
                           never, (int)(sizeof(kButtons) / sizeof(kButtons[0])));
        shown++;
    }

    if (shown == 0) {
        ImGui::TextColored(kOn, "No faults detected.");
    }
}

void SbcDiagnosticsWindow::DrawAxes(const SbcFrame &f, bool live)
{
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##sbc_axes", 7, flags)) {
        return;
    }

    ImGui::TableSetupColumn("Axis");
    ImGui::TableSetupColumn("Raw");
    ImGui::TableSetupColumn("Decoded");
    ImGui::TableSetupColumn("Travel", ImGuiTableColumnFlags_WidthFixed,
                            g_viewport_mgr.Scale(ImVec2(150, 0)).x);
    ImGui::TableSetupColumn("Min");
    ImGui::TableSetupColumn("Max");
    ImGui::TableSetupColumn("Wire bytes");
    ImGui::TableHeadersRow();

    for (int i = 0; i < kAxisCount; i++) {
        const AxisDesc &d = kAxes[i];
        const AxisStat &a = m_axis[i];
        const int32_t v = live ? AxisValue(f, i) : 0;
        const int32_t span = d.hi - d.lo;
        const float frac = span ? (float)(v - d.lo) / (float)span : 0.0f;

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::TextColored(kDim, "%s", d.label);

        ImGui::TableNextColumn();
        ImGui::PushFont(g_font_mgr.m_fixed_width_font);
        ImGui::Text("0x%04X", (unsigned)(uint16_t)v);
        ImGui::PopFont();

        ImGui::TableNextColumn();
        if (d.bipolar) {
            ImGui::Text("%+6d", (int)v);
        } else {
            ImGui::Text("%6u", (unsigned)v);
        }

        ImGui::TableNextColumn();
        char overlay[32];
        int tenths = (int)(frac * 1000.0f + 0.5f);
        if (tenths < 0) tenths = 0;
        if (tenths > 1000) tenths = 1000;
        snprintf(overlay, sizeof(overlay), "%d.%d%%", tenths / 10, tenths % 10);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              (frac > 0.01f && frac < 0.99f) ? kOn : kOff);
        ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0.0f), overlay);
        ImGui::PopStyleColor();

        ImGui::TableNextColumn();
        if (m_samples) {
            ImGui::TextColored(kDim, "0x%04X", (unsigned)(uint16_t)a.min_v);
        } else {
            ImGui::TextColored(kOff, "-");
        }

        ImGui::TableNextColumn();
        if (m_samples) {
            ImGui::TextColored(a.max_v >= d.hi - span / 64 ? kWarn : kDim,
                               "0x%04X", (unsigned)(uint16_t)a.max_v);
        } else {
            ImGui::TextColored(kOff, "-");
        }

        ImGui::TableNextColumn();
        ImGui::TextColored(kOff, "%s", d.wire);
    }

    ImGui::EndTable();
}

void SbcDiagnosticsWindow::DrawSwitches(const SbcFrame &f, bool live)
{
    const uint8_t levels = live ? f.toggle_levels : 0;
    const uint64_t buttons = live ? f.buttons : 0;

    ImGui::TextColored(kDim, "Toggle switches (latched physical level)");
    ImGui::SameLine();
    ImGui::PushFont(g_font_mgr.m_fixed_width_font);
    ImGui::TextColored(kOff, "raw toggle_levels = 0x%02X", (unsigned)levels);
    ImGui::PopFont();

    for (size_t i = 0; i < sizeof(kToggles) / sizeof(kToggles[0]); i++) {
        const ToggleDesc &t = kToggles[i];
        const bool on = (levels & t.mask) != 0;
        const bool pressed = (buttons & (1ULL << t.press_bit)) != 0;
        ImGui::TextColored(on ? kOn : kOff, "%s", on ? "*" : "o");
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(on ? kOn : kDim, "%-24s", t.label);
        ImGui::SameLine();
        ImGui::PushFont(g_font_mgr.m_fixed_width_font);
        ImGui::TextColored(kOff, "mask 0x%02X  bit %d", (unsigned)t.mask,
                           t.press_bit);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(pressed ? kOn : kOff, "%s",
                           pressed ? "[pressing]" : "          ");
    }
    ImGui::TextColored(kOff,
                       "Bits 0-1 of the same wire byte are COM5 and Sight "
                       "Change, not toggles.");

    ImGui::Spacing();

    const uint8_t gear = live ? f.gear_lever : 0;
    const GearDecode g = DecodeGear(gear);
    ImGui::TextColored(kDim, "Gear lever");
    ImGui::SameLine();
    ImGui::PushFont(g_font_mgr.m_fixed_width_font);
    ImGui::Text("raw 0x%02X (%u)", (unsigned)gear, (unsigned)gear);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(g.reverse ? kWarn : kOn, "[%s]", g.glyph);
    ImGui::SameLine();
    ImGui::TextColored(kOff, "encoding: %s", g.enc);
    ImGui::SameLine();
    HelpMarker("Two gear encodings are on record and they do not collide: "
               "254/255 plus 1-5 (xemu's internal encoding, field-verified) "
               "and 7-15 (the encoding documented on the wire report). Which "
               "one your controller uses is itself a finding - include it in "
               "any report.");

    const uint8_t tuner_raw = (live && f.raw_len > 24) ? f.raw[24] : 0;
    ImGui::TextColored(kDim, "Tuner dial");
    ImGui::SameLine();
    ImGui::PushFont(g_font_mgr.m_fixed_width_font);
    ImGui::Text("%2u/15", (unsigned)(live ? f.tuner_dial : 0));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(kOff,
                       "raw byte 24 = 0x%02X (low nibble 0x%X, upper nibble "
                       "0x%X)",
                       (unsigned)tuner_raw, (unsigned)(tuner_raw & 0x0F),
                       (unsigned)(tuner_raw >> 4));
    ImGui::SameLine();
    HelpMarker("9 o'clock is 0, 6 o'clock is 12. The upper nibble of this byte "
               "is undocumented globally - if it is ever non-zero on your "
               "hardware, that is a genuine finding: record a capture.");
}

void SbcDiagnosticsWindow::DrawButtons(const SbcFrame &f, bool live)
{
    const uint64_t buttons = live ? f.buttons : 0;

    ImGui::PushFont(g_font_mgr.m_fixed_width_font);
    ImGui::TextColored(kOff, "raw buttons = 0x%010" PRIX64, buttons);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(kOff, "(bits 0-38; wire bytes 2-6)");

    const int kCols = 4;
    if (!ImGui::BeginTable("##sbc_buttons", kCols,
                           ImGuiTableFlags_SizingStretchSame)) {
        return;
    }
    for (size_t i = 0; i < sizeof(kButtons) / sizeof(kButtons[0]); i++) {
        if ((i % kCols) == 0) {
            ImGui::TableNextRow();
        }
        ImGui::TableNextColumn();

        const ButtonDesc &b = kButtons[i];
        const bool on = (buttons & (1ULL << b.bit)) != 0;
        const bool ever = (m_buttons_ever & (1ULL << b.bit)) != 0;
        const bool suspect = !ever && m_samples >= (uint64_t)kMinFaultSamples;

        ImGui::TextColored(on ? kOn : kOff, "%s", on ? "*" : "o");
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(on ? kOn : (suspect ? kOff : kDim), "%2d %s", b.bit,
                           b.label);
        if (suspect) {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(kOff, "-");
        }
    }
    ImGui::EndTable();
}

void SbcDiagnosticsWindow::DrawRawReport(const SbcDeviceState &st,
                                         uint64_t now_ms)
{
    const bool live = st.frames_received > 0;
    int n = live ? (int)st.frame.raw_len : 0;
    if (n > SBC_RAW_REPORT_MAX) {
        n = SBC_RAW_REPORT_MAX;
    }

    ImGui::TextColored(kDim, "raw_len");
    ImGui::SameLine();
    ImGui::TextColored(
        (live && st.frame.raw_len != SBC_RAW_REPORT_EXPECTED) ? kWarn : kDim,
        "%d", live ? (int)st.frame.raw_len : 0);
    ImGui::SameLine();
    ImGui::TextColored(kOff, "(expected %d)", SBC_RAW_REPORT_EXPECTED);
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextColored(kDim, "timestamp");
    ImGui::SameLine();
    ImGui::Text("%" PRIu64 " us", live ? st.frame.timestamp_us : 0);

    ImGui::PushFont(g_font_mgr.m_fixed_width_font);
    for (int row = 0; row < (n + 15) / 16; row++) {
        ImGui::TextColored(kOff, "%02X:", row * 16);
        for (int col = 0; col < 16; col++) {
            const int i = row * 16 + col;
            if (i >= n) {
                break;
            }
            ImGui::SameLine(0.0f, 6.0f);
            const bool hot = (now_ms - m_raw_changed_ms[i]) < 400 &&
                             m_raw_changed_ms[i] != 0;
            ImGui::TextColored(hot ? kOn : kDim, "%02X",
                               (unsigned)st.frame.raw[i]);
        }
        ImGui::TextColored(kOff, "    ");
        for (int col = 0; col < 16; col++) {
            const int i = row * 16 + col;
            if (i >= n) {
                break;
            }
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextColored(m_raw_ever_changed[i] ? kInfo : kOff, "%s",
                               m_raw_ever_changed[i] ? "^^" : "..");
        }
    }
    ImGui::PopFont();
    if (n == 0) {
        ImGui::TextColored(kOff, "(no frame received yet)");
    }
    ImGui::TextColored(kOff,
                       "green = byte changed in the last 0.4 s;  ^^ = byte has "
                       "changed at least once this session");

    if (ImGui::TreeNode("Wire field map")) {
        for (size_t i = 0; i < sizeof(kWireMap) / sizeof(kWireMap[0]); i++) {
            const WireField &w = kWireMap[i];
            char range[32];
            if (w.first == w.last) {
                snprintf(range, sizeof(range), "%2d   ", w.first);
            } else {
                snprintf(range, sizeof(range), "%2d-%-2d", w.first, w.last);
            }
            ImGui::PushFont(g_font_mgr.m_fixed_width_font);
            ImGui::TextColored(w.unknown ? kWarn : kDim, "%s  %s", range,
                               w.name);
            ImGui::PopFont();
        }
        ImGui::TextWrapped(
            "Derived from XIDSteelBattalionReport (the emulated descriptor is "
            "byte-identical to real hardware). The three fields in amber have "
            "never been dumped by anyone - a capture from a real controller "
            "settles them.");
        ImGui::TreePop();
    }
}

void SbcDiagnosticsWindow::CopyReportToClipboard(const SbcDeviceState &st,
                                                 const char *detail)
{
    std::string s;
    char line[512];

    snprintf(line, sizeof(line),
             "PenguinBox Steel Battalion controller report\n"
             "status=%s detail=\"%s\"\n"
             "frames=%" PRIu64 " rate=%.1f/s transfer_errors=%" PRIu64
             " malformed=%" PRIu64 "\n"
             "source=%s samples=%" PRIu64 "\n",
             StatusName(st.status), detail, st.frames_received, (double)m_fps,
             st.transfer_errors, st.malformed_reports,
             sbc_device_synthetic_enabled() ? "synthetic/replay" : "real device",
             m_samples);
    s += line;

    if (st.frames_received > 0) {
        int n = (int)st.frame.raw_len;
        if (n > SBC_RAW_REPORT_MAX) {
            n = SBC_RAW_REPORT_MAX;
        }
        snprintf(line, sizeof(line), "raw_len=%d raw=", n);
        s += line;
        for (int i = 0; i < n; i++) {
            snprintf(line, sizeof(line), "%02X ", (unsigned)st.frame.raw[i]);
            s += line;
        }
        s += "\n";
        snprintf(line, sizeof(line),
                 "buttons=0x%010" PRIX64 " toggles=0x%02X gear=0x%02X "
                 "tuner=%u\n",
                 st.frame.buttons, (unsigned)st.frame.toggle_levels,
                 (unsigned)st.frame.gear_lever, (unsigned)st.frame.tuner_dial);
        s += line;
    }

    s += "axis,min,max,rest_jitter\n";
    for (int i = 0; i < kAxisCount; i++) {
        snprintf(line, sizeof(line), "%s,0x%04X,0x%04X,%d\n", kAxes[i].label,
                 (unsigned)(uint16_t)m_axis[i].min_v,
                 (unsigned)(uint16_t)m_axis[i].max_v, (int)m_axis[i].rest_spread);
        s += line;
    }

    snprintf(line, sizeof(line), "buttons_ever=0x%010" PRIX64 "\n",
             m_buttons_ever);
    s += line;
    s += "raw_bytes_ever_changed=";
    for (int i = 0; i < SBC_RAW_REPORT_MAX; i++) {
        s += m_raw_ever_changed[i] ? "1" : "0";
    }
    s += "\n";

    ImGui::SetClipboardText(s.c_str());
    xemu_queue_notification("SBC: support report copied to clipboard");
}

void SbcDiagnosticsWindow::Draw()
{
    ServicePendingFileDialogs();

    const SimProfile *gate = sim_gate_active_profile();
    const bool gating = (gate != NULL);
    const bool gate_opened = gating && !m_gate_was_active;
    m_gate_was_active = gating;

    if (gating) {
        m_is_open = true;
    }

    if (!m_is_open) {
        return;
    }

    SbcDeviceState st;
    memset(&st, 0, sizeof(st));
    sbc_device_get_state(&st);
    const char *detail = st.status_detail ? st.status_detail : "";

    const uint64_t now_ms = (uint64_t)SDL_GetTicks();
    if (gate_opened) {
        m_gate_open_ms = now_ms;
    }
    UpdateRate(st, now_ms);
    if (st.frames_received > 0 &&
        (!m_have_last_seq || st.frame.seq != m_last_seq)) {
        Observe(st.frame, now_ms);
        m_last_seq = st.frame.seq;
        m_have_last_seq = true;
    }
    const bool live = st.frames_received > 0;

    if (gate_opened) {
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImVec2 want = g_viewport_mgr.Scale(ImVec2(760, 860));
        if (want.x > vp->WorkSize.x * 0.95f) {
            want.x = vp->WorkSize.x * 0.95f;
        }
        if (want.y > vp->WorkSize.y * 0.95f) {
            want.y = vp->WorkSize.y * 0.95f;
        }
        ImGui::SetNextWindowSize(want, ImGuiCond_Always);
        ImGui::SetNextWindowPos(
            ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowFocus();
    } else {
        ImGui::SetNextWindowSize(g_viewport_mgr.Scale(ImVec2(720, 660)),
                                 ImGuiCond_FirstUseEver);
    }

    char title[224];
    if (gating) {
        snprintf(title, sizeof(title), "Pre-flight check - %s###sbc_diag",
                 gate->display_name);
    } else {
        snprintf(title, sizeof(title),
                 "Steel Battalion Controller###sbc_diag");
    }

    if (!ImGui::Begin(title, gating ? NULL : &m_is_open,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (gating) {
        DrawPreflightChecklist(gate, st, now_ms);
        ImGui::SeparatorText("Device");
    }

    DrawStatusBanner(st, detail);
    ImGui::Spacing();
    DrawLinkStats(st, now_ms);

    ImGui::SeparatorText("Source");
    DrawSourceControls(st);

    if (!gating) {
        ImGui::SeparatorText("Report capture (send us this file)");
        DrawCaptureControls(st);
    }

    ImGui::SeparatorText("Fault hints");
    DrawFaultHints();

    ImGui::SeparatorText("Axes");
    DrawAxes(st.frame, live);

    ImGui::SeparatorText("Switches, gear and tuner");
    DrawSwitches(st.frame, live);

    ImGui::SeparatorText("Buttons");
    DrawButtons(st.frame, live);

    ImGui::SeparatorText("Raw report");
    DrawRawReport(st, now_ms);

    ImGui::Separator();
    if (ImGui::Button("Reset session stats")) {
        ResetStats();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy support report")) {
        CopyReportToClipboard(st, detail);
    }
    ImGui::SameLine();
    ImGui::TextColored(kOff,
                       "Read-only diagnostics: this panel never writes to the "
                       "controller or the running game.");

    ImGui::End();
}
