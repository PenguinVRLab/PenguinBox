//
// xemu User Interface
//
// Copyright (C) 2020-2022 Matt Borgerson
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <functional>
#include <assert.h>
#include <fpng.h>

#include <deque>
#include <vector>
#include <string>
#include <memory>

#include "actions.hh"
#include "common.hh"
#include "../xemu-input.h"
#include "xemu-hud.h"
#include "misc.hh"
#include "gl-helpers.hh"
#include "input-manager.hh"
#include "snapshot-manager.hh"
#include "viewport-manager.hh"
#include "font-manager.hh"
#include "scene.hh"
#include "scene-manager.hh"
#include "main-menu.hh"
#include "popup-menu.hh"
#include "notifications.hh"
#include "sbc-diagnostics.hh"
#include "sbc-overlay.hh"
#include "monitor.hh"
#include "debug.hh"
#include "welcome.hh"
#include "menubar.hh"
#include "compat.hh"
#include "../xemu-net.h"
#include "hw/xbox/nv2a/pgraph/vk/vr_public.h"
#include "hw/xbox/nv2a/pgraph/vk/vr_ui_panel.h"
#include "hw/xbox/nv2a/pgraph/vk/vr_padxr_actions.h"
#if defined(_WIN32)
#include "update.hh"
#endif

bool g_screenshot_pending;
const char *g_snapshot_pending_load_name;

float g_main_menu_height;

static ImGuiStyle g_base_style;
static float g_last_scale;
static int g_vsync;
static GLuint g_tex;
static bool g_flip_req;


static void InitializeStyle()
{
    g_font_mgr.Rebuild();

    ImGui::StyleColorsDark();
    ImVec4 *c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text]                  = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
    c[ImGuiCol_TextDisabled]          = ImVec4(0.86f, 0.93f, 0.89f, 0.28f);
    c[ImGuiCol_WindowBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    c[ImGuiCol_ChildBg]               = ImVec4(0.06f, 0.06f, 0.06f, 0.98f);
    c[ImGuiCol_PopupBg]               = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    c[ImGuiCol_Border]                = ImVec4(0.11f, 0.11f, 0.11f, 0.60f);
    c[ImGuiCol_BorderShadow]          = ImVec4(0.16f, 0.16f, 0.16f, 0.00f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
    c[ImGuiCol_TitleBg]               = ImVec4(0.20f, 0.51f, 0.18f, 1.00f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.16f, 0.16f, 0.16f, 0.75f);
    c[ImGuiCol_MenuBarBg]             = ImVec4(0.14f, 0.14f, 0.14f, 0.00f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.16f, 0.16f, 0.16f, 0.00f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_CheckMark]             = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
    c[ImGuiCol_SliderGrab]            = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    c[ImGuiCol_SliderGrabActive]      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    c[ImGuiCol_Button]                = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
    c[ImGuiCol_Header]                = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_Separator]             = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
    c[ImGuiCol_SeparatorHovered]      = ImVec4(0.13f, 0.87f, 0.16f, 0.78f);
    c[ImGuiCol_SeparatorActive]       = ImVec4(0.25f, 0.75f, 0.10f, 1.00f);
    c[ImGuiCol_ResizeGrip]            = ImVec4(0.47f, 0.83f, 0.49f, 0.04f);
    c[ImGuiCol_ResizeGripHovered]     = ImVec4(0.28f, 0.71f, 0.25f, 0.78f);
    c[ImGuiCol_ResizeGripActive]      = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
    c[ImGuiCol_Tab]                   = ImVec4(0.26f, 0.67f, 0.23f, 0.95f);
    c[ImGuiCol_TabHovered]            = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_TabActive]             = ImVec4(0.24f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_TabUnfocused]          = ImVec4(0.21f, 0.54f, 0.19f, 0.99f);
    c[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.24f, 0.60f, 0.21f, 1.00f);
    c[ImGuiCol_PlotLines]             = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
    c[ImGuiCol_PlotLinesHovered]      = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
    c[ImGuiCol_PlotHistogram]         = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
    c[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
    c[ImGuiCol_TextSelectedBg]        = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
    c[ImGuiCol_DragDropTarget]        = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    c[ImGuiCol_NavHighlight]          = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.16f, 0.16f, 0.16f, 0.73f);

    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 6.0;
    s.FrameRounding = 6.0;
    s.PopupRounding = 6.0;
    g_base_style = s;
}

void xemu_hud_init(SDL_Window* window, void* sdl_gl_context)
{
    xemu_monitor_init();
    g_vsync = g_config.display.window.vsync;

    InitCustomRendering();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = NULL;

    ImGui_ImplSDL3_InitForOpenGL(window, sdl_gl_context);
    ImGui_ImplOpenGL3_Init("#version 150");
    ImPlot::CreateContext();

#if defined(_WIN32)
    if (!g_config.general.show_welcome && g_config.general.updates.check) {
        update_window.CheckForUpdates();
    }
#endif
    g_last_scale = g_viewport_mgr.m_scale;
    InitializeStyle();
    g_main_menu.SetNextViewIndex(g_config.general.last_viewed_menu_index);
    first_boot_window.is_open = g_config.general.show_welcome;
}

void xemu_hud_cleanup(void)
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void xemu_hud_process_sdl_events(SDL_Event *event)
{
    if (g_main_menu.ConsumeRebindEvent(event)) {
        return;
    }

    ImGui_ImplSDL3_ProcessEvent(event);
}

void xemu_hud_should_capture_kbd_mouse(int *kbd, int *mouse)
{
    ImGuiIO& io = ImGui::GetIO();
    if (kbd) *kbd = io.WantCaptureKeyboard;
    if (mouse) *mouse = io.WantCaptureMouse;
}

void xemu_hud_set_framebuffer_texture(GLuint tex, bool flip)
{
    g_tex = tex;
    g_flip_req = flip;
}

static bool g_vr_quick_open;

static void VrQuickMenuEvents()
{
    uint32_t ev = vr_ui_panel_take_events();
    if (ev & PADXR_EVENT_MENU_OPEN) {
        g_vr_quick_open = true;
    }
    if (ev & PADXR_EVENT_MENU_CLOSE) {
        g_vr_quick_open = false;
    }
}

static void VrQuickMenuDraw()
{
    if (!g_vr_quick_open) {
        return;
    }
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f,
                                   io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("VR quick menu", &g_vr_quick_open,
                     ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetWindowFocus();
        }
        if (ImGui::Button("Resume")) {
            g_vr_quick_open = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Recenter")) {
            xemu_vr_request_recenter();
        }
        ImGui::SliderFloat("Screen distance (m)", &g_config.vr.screen_distance,
                           0.5f, 5.0f, "%.2f");
        ImGui::SliderFloat("Screen height (m)", &g_config.vr.screen_height,
                           0.5f, 3.0f, "%.2f");
        ImGui::SliderFloat("Vertical offset (m)",
                           &g_config.vr.screen_vertical_offset, -1.0f, 1.0f,
                           "%.2f");
        ImGui::SliderFloat("Curve (deg)", &g_config.vr.screen_arc, 0.0f,
                           180.0f, "%.0f");
    }
    ImGui::End();
}

static bool g_vr_quick_was_open;
static bool g_vr_quick_paused;

static void VrQuickMenuFollowPause()
{
    if (g_vr_quick_paused && runstate_is_running()) {
        g_vr_quick_paused = false;
    }
    if (g_vr_quick_open == g_vr_quick_was_open) {
        return;
    }
    g_vr_quick_was_open = g_vr_quick_open;
    if (g_vr_quick_open) {
        if (xemu_vr_active() && !xemu_net_is_enabled() &&
            runstate_is_running()) {
            vm_stop(RUN_STATE_PAUSED);
            g_vr_quick_paused = true;
            fprintf(stderr, "PenguinBox: VR quick menu paused the guest "
                            "(offline)\n");
        }
        return;
    }
    if (g_vr_quick_paused) {
        g_vr_quick_paused = false;
        if (runstate_check(RUN_STATE_PAUSED)) {
            vm_start();
            fprintf(stderr, "PenguinBox: VR quick menu resumed the guest\n");
        }
    }
}

static void VrPanelRender(ImDrawData *dd)
{
    static GLuint fbo, tex, pbo[2];
    static int frame;
    static bool primed[2];
    static bool warned_slow;
    static std::vector<uint8_t> rows;

    if (!vr_ui_panel_wanted() || dd == NULL || dd->DisplaySize.x <= 0 ||
        dd->DisplaySize.y <= 0) {
        primed[0] = primed[1] = false;
        return;
    }
    const size_t bytes = (size_t)VR_UI_PANEL_W * VR_UI_PANEL_H * 4;
    if (fbo == 0) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, VR_UI_PANEL_W, VR_UI_PANEL_H,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex, 0);
        glGenBuffers(2, pbo);
        for (int i = 0; i < 2; i++) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, bytes, NULL, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        rows.resize(bytes);
        fprintf(stderr, "PenguinBox: UI panel FBO %dx%d (%s)\n", VR_UI_PANEL_W,
                VR_UI_PANEL_H,
                glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                        GL_FRAMEBUFFER_COMPLETE ? "complete" : "INCOMPLETE");
    }

    GLint last_fb = 0, last_vp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &last_fb);
    glGetIntegerv(GL_VIEWPORT, last_vp);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, VR_UI_PANEL_W, VR_UI_PANEL_H);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    const ImVec2 scale = dd->FramebufferScale;
    dd->FramebufferScale = ImVec2(VR_UI_PANEL_W / dd->DisplaySize.x,
                                  VR_UI_PANEL_H / dd->DisplaySize.y);
    ImGui_ImplOpenGL3_RenderDrawData(dd);
    dd->FramebufferScale = scale;

    const int cur = frame & 1, prev = cur ^ 1;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[cur]);
    glReadPixels(0, 0, VR_UI_PANEL_W, VR_UI_PANEL_H, GL_RGBA,
                 GL_UNSIGNED_BYTE, 0);
    primed[cur] = true;

    if (primed[prev]) {
        const uint64_t t0 = SDL_GetTicksNS();
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[prev]);
        const uint8_t *src = (const uint8_t *)glMapBufferRange(
            GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT);
        if (src) {
            const size_t stride = (size_t)VR_UI_PANEL_W * 4;
            for (int y = 0; y < VR_UI_PANEL_H; y++) {
                const uint8_t *s_row = src + (size_t)(VR_UI_PANEL_H - 1 - y) * stride;
                uint8_t *d_row = rows.data() + (size_t)y * stride;
                for (int x = 0; x < VR_UI_PANEL_W; x++) {
                    const uint8_t *sp = s_row + x * 4;
                    uint8_t *dp = d_row + x * 4;
                    const unsigned a = sp[3];
                    if (a == 0 || a == 255) {
                        dp[0] = sp[0];
                        dp[1] = sp[1];
                        dp[2] = sp[2];
                    } else {
                        { unsigned v = sp[0] * 255u / a; dp[0] = (uint8_t)(v > 255u ? 255u : v); }
                        { unsigned v = sp[1] * 255u / a; dp[1] = (uint8_t)(v > 255u ? 255u : v); }
                        { unsigned v = sp[2] * 255u / a; dp[2] = (uint8_t)(v > 255u ? 255u : v); }
                    }
                    dp[3] = (uint8_t)a;
                }
            }
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            vr_ui_panel_publish(rows.data(), VR_UI_PANEL_W, VR_UI_PANEL_H);
        }
        const double ms = (SDL_GetTicksNS() - t0) / 1e6;
        if (ms > 4.0 && !warned_slow) {
            warned_slow = true;
            fprintf(stderr, "PenguinBox: UI panel readback %.2f ms on the UI "
                            "thread (over the 4 ms budget)\n", ms);
        }
    }
    frame++;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, last_fb);
    glViewport(last_vp[0], last_vp[1], last_vp[2], last_vp[3]);
}

void xemu_hud_update(void)
{
    ImGuiIO& io = ImGui::GetIO();
    uint32_t now = SDL_GetTicks();

    g_viewport_mgr.Update();
    g_font_mgr.Update();
    if (g_last_scale != g_viewport_mgr.m_scale) {
        ImGuiStyle &style = ImGui::GetStyle();
        style = g_base_style;
        style.ScaleAllSizes(g_viewport_mgr.m_scale);
        g_last_scale = g_viewport_mgr.m_scale;
    }

    if (!first_boot_window.is_open) {
        int ww, wh;
        SDL_GetWindowSizeInPixels(xemu_get_window(), &ww, &wh);
        RenderFramebuffer(g_tex, ww, wh, g_flip_req);
    }

    ImGui_ImplOpenGL3_NewFrame();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL3_NewFrame();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    g_input_mgr.Update();

    ImGui::NewFrame();
    ProcessKeyboardShortcuts();
    VrQuickMenuEvents();

#if defined(CONFIG_RENDERDOC)
    if (g_capture_renderdoc_frame) {
        nv2a_dbg_renderdoc_capture_frames(1, false);
        g_capture_renderdoc_frame = false;
    }
#endif

    if (g_config.display.ui.show_menubar && !first_boot_window.is_open) {
        static uint32_t last_check = 0;
        float alpha = 1.0;
        const uint32_t timeout = 5000;
        const float fade_duration = 1000.0;
        bool menu_wakeup = g_input_mgr.MouseMoved();
        if (menu_wakeup) {
            last_check = now;
        }
        if ((now-last_check) > timeout) {
            if (g_config.display.ui.use_animations) {
                float t = fmin((float)((now-last_check)-timeout)/fade_duration, 1);
                alpha = 1-t;
                if (t >= 1) {
                    alpha = 0;
                }
            } else {
                alpha = 0;
            }
        }
        if (alpha > 0.0) {
            ImVec4 tc = ImGui::GetStyle().Colors[ImGuiCol_Text];
            tc.w = alpha;
            ImGui::PushStyleColor(ImGuiCol_Text, tc);
            ImGui::SetNextWindowBgAlpha(alpha);
            ShowMainMenu();
            ImGui::PopStyleColor();
        } else {
            g_main_menu_height = 0;
        }
    }

    static uint32_t last_mouse_move = 0;
    if (g_input_mgr.MouseMoved()) {
        last_mouse_move = now;
    }

    if (g_config.display.ui.hide_cursor && (now - last_mouse_move) > 3000) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow) &&
        !g_scene_mgr.IsDisplayingScene()) {

        bool menu_button = false;
        uint32_t buttons = g_input_mgr.CombinedButtons();
        if (buttons & CONTROLLER_BUTTON_GUIDE) {
            menu_button = true;
        }

        if ((buttons & CONTROLLER_BUTTON_BACK) &&
            (buttons & CONTROLLER_BUTTON_START)) {
            menu_button = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_F1)) {
            g_scene_mgr.PushScene(g_main_menu);
        } else if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
            g_scene_mgr.PushScene(g_popup_menu);
        } else if (menu_button) {
            g_scene_mgr.PushScene(g_popup_menu);
        } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            xemu_toggle_fullscreen();
        }

        SbcOverlayProcessHotkey();

        if (!xemu_input_keyboard_feeds_sbc()) {
            bool mod_key_down = ImGui::IsKeyDown(ImGuiKey_ModShift);
            for (int f_key = 0; f_key < 4; ++f_key) {
                if (ImGui::IsKeyPressed((enum ImGuiKey)(ImGuiKey_F5 + f_key))) {
                    ActionActivateBoundSnapshot(f_key, mod_key_down);
                    break;
                }
            }
        }
    }

    first_boot_window.Draw();
    monitor_window.Draw();
    apu_window.Draw();
    video_window.Draw();
    sbc_diagnostics_window.Draw();
    compatibility_reporter_window.Draw();
#if defined(_WIN32)
    update_window.Draw();
#endif
    g_scene_mgr.Draw();
    SimBootDraw();
    VrQuickMenuDraw();
    VrQuickMenuFollowPause();
    vr_ui_panel_set_open(xemu_vr_active() &&
                         (g_vr_quick_open || g_scene_mgr.IsDisplayingScene()));
    if (!first_boot_window.is_open) notification_manager.Draw();
    g_snapshot_mgr.Draw();
    SbcOverlayDraw();

}

void xemu_hud_render()
{
    ImGui::Render();
    VrPanelRender(ImGui::GetDrawData());
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (g_vsync != g_config.display.window.vsync) {
        g_vsync = g_config.display.window.vsync;
        SDL_GL_SetSwapInterval(g_vsync ? 1 : 0);
    }

    if (g_screenshot_pending) {
        SaveScreenshot(g_tex, g_flip_req);
        g_screenshot_pending = false;
    }
}
