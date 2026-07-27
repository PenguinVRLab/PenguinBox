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
#include "monitor.hh"
#include "imgui.h"
#include "misc.hh"
#include "font-manager.hh"

static int   Stricmp(const char* str1, const char* str2)         { int d; while ((d = toupper(*str2) - toupper(*str1)) == 0 && *str1) { str1++; str2++; } return d; }
static char* Strdup(const char *str)                             { size_t len = strlen(str) + 1; void* buf = malloc(len); IM_ASSERT(buf); return (char*)memcpy(buf, (const void*)str, len); }
static void  Strtrim(char* str)                                  { char* str_end = str + strlen(str); while (str_end > str && str_end[-1] == ' ') str_end--; *str_end = 0; }

MonitorWindow::MonitorWindow()
{
    is_open = false;
    memset(InputBuf, 0, sizeof(InputBuf));
    HistoryPos = -1;
    AutoScroll = true;
    ScrollToBottom = false;
}
MonitorWindow::~MonitorWindow()
{
}

void MonitorWindow::Draw()
{
    if (!is_open) return;
    int style_pop_cnt = PushWindowTransparencySettings(true) + 1;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImU32(ImColor(0, 0, 0, 80)));
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 window_pos = ImVec2(0,io.DisplaySize.y/2);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y/2), ImGuiCond_Appearing);
    if (ImGui::Begin("Monitor", &is_open, ImGuiWindowFlags_NoCollapse)) {
        const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4,1));
        ImGui::PushFont(g_font_mgr.m_fixed_width_font);

        char *buffer = xemu_get_monitor_buffer();
        size_t buffer_len = strlen(buffer);
        int num_newlines = 2;
        const char *start = buffer;
        while (start) {
            start = strchr(start, '\n');
            if (start) {
                ++num_newlines;
                ++start;
            }
        }
        float input_height = fmax(ImGui::GetWindowHeight(),
                                  g_font_mgr.m_fixed_width_font->FontSize * num_newlines);

        ImGui::PushID("#MonitorOutput");
        ImGui::InputTextMultiline("",
                                  buffer,
                                  buffer_len + 1,
                                  ImVec2(-1.0f, input_height),
                                  ImGuiInputTextFlags_ReadOnly|ImGuiInputTextFlags_NoUndoRedo);
        ImGui::PopID();
        ImGui::PopFont();

        if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
            ImGui::SetScrollHereY(1.0f);
        }
        ScrollToBottom = false;

        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::Separator();
        bool reclaim_focus = ImGui::IsWindowAppearing();

        ImGui::SetNextItemWidth(-1);
        ImGui::PushFont(g_font_mgr.m_fixed_width_font);
        if (ImGui::InputText("#commandline", InputBuf, IM_ARRAYSIZE(InputBuf), ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CallbackCompletion|ImGuiInputTextFlags_CallbackHistory, &TextEditCallbackStub, (void*)this)) {
            char* s = InputBuf;
            Strtrim(s);
            if (s[0])
                ExecCommand(s);
            strcpy(s, "");
            reclaim_focus = true;
        }
        ImGui::PopFont();
        ImGui::SetItemDefaultFocus();
        if (reclaim_focus) {
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(style_pop_cnt);
}

void MonitorWindow::ToggleOpen(void)
{
    is_open = !is_open;
}

void MonitorWindow::ExecCommand(const char* command_line)
{
    xemu_run_monitor_command(command_line);

    HistoryPos = -1;
    for (int i = History.Size-1; i >= 0; i--)
        if (Stricmp(History[i], command_line) == 0)
        {
            free(History[i]);
            History.erase(History.begin() + i);
            break;
        }
    History.push_back(Strdup(command_line));

    ScrollToBottom = true;
}

int MonitorWindow::TextEditCallbackStub(ImGuiInputTextCallbackData* data)
{
    MonitorWindow* console = (MonitorWindow*)data->UserData;
    return console->TextEditCallback(data);
}

int MonitorWindow::TextEditCallback(ImGuiInputTextCallbackData* data)
{
    switch (data->EventFlag)
    {
    case ImGuiInputTextFlags_CallbackHistory:
        {
            const int prev_history_pos = HistoryPos;
            if (data->EventKey == ImGuiKey_UpArrow)
            {
                if (HistoryPos == -1)
                    HistoryPos = History.Size - 1;
                else if (HistoryPos > 0)
                    HistoryPos--;
            }
            else if (data->EventKey == ImGuiKey_DownArrow)
            {
                if (HistoryPos != -1)
                    if (++HistoryPos >= History.Size)
                        HistoryPos = -1;
            }

            if (prev_history_pos != HistoryPos)
            {
                const char* history_str = (HistoryPos >= 0) ? History[HistoryPos] : "";
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, history_str);
            }
        }
    }
    return 0;
}

MonitorWindow monitor_window;
