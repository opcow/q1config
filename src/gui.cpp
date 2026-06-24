#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // keep std::min/std::max usable (windows.h would macro them)
#endif
#include <windows.h>
#endif
#include "gui.h"
#include "hid.h"
#include "keycodes.h"
#include "layout.h"
#include "preset.h"
#include "protocol.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>  // DwmSetWindowAttribute — rounded window corners (Win11)
#endif
#include <nfd.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include "../fonts/inter_regular.inl"

// ── constants ─────────────────────────────────────────────────────────────────
static constexpr float KEY_U   = 44.0f;
static constexpr float KEY_GAP = 3.0f;

// Highlight for a "selected" button (active layer, current keycode) — a bright
// blue that stands out against the neutral buttons.
static const ImVec4 SELECTED_BTN = ImVec4(0.46f, 0.74f, 1.00f, 1.00f);

// Blue accent for command/action buttons (Connect, Save, Reset, layer, …).
// Keycode-field and picker-grid buttons keep the neutral theme colors.
static void pushPrimaryButtonStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.55f, 0.90f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.66f, 1.00f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.14f, 0.44f, 0.78f, 1.0f));
}
static void popPrimaryButtonStyle() { ImGui::PopStyleColor(3); }

static const char* LAYER_NAMES[] = {"0 MAC_BASE","1 MAC_FN","2 WIN_BASE","3 WIN_FN"};
// Slots are named TD0..TDn so a slot's name is its index (matches the firmware).
static const char* IND_NAMES[]   = {"Caps Lock","Caps Word","Win FN layer","Num Lock",
                                    "Scroll Lock","Mac FN layer","Windows mode","One-shot mod"};
static const char* FLAG_NAMES[]  = {"Caps Word","Permissive hold","Hold on other key press","Retro tapping","Auto Shift"};
static const char* DB_METHOD_LABELS[] = {"None","Symmetric Defer","Symmetric Eager","Asymmetric Eager/Defer"};

// ── app state ─────────────────────────────────────────────────────────────────
static HidDevice g_dev;
static bool      g_connected = false;
static char      g_statusMsg[128] = "Not connected.";

static int      g_layer = 2;
static uint16_t g_keymap[6][16] = {};

static TdSlot  g_td[TD_SLOT_COUNT]  = {};
static bool    g_showAllTd = false;

static GlobalState  g_global = {};
static FeatState    g_feat   = {};
static IndState     g_ind[INDICATOR_COUNT] = {};
static Combo        g_combo[COMBO_SLOT_COUNT] = {};
static KeyOverride  g_ko[KO_SLOT_COUNT]       = {};

// identify (non-blocking poll in render loop)
static bool  g_identifying   = false;
static float g_identTimeout  = 0.0f;
static float g_identHighlight = 0.0f;
static int   g_identHlRow = -1, g_identHlCol = -1;

// key editor
static bool     g_editorOpen = false;
static int      g_editorRow  = -1, g_editorCol = -1;
static uint16_t g_editorKc   = 0;

// cached layout extents
static float KB_W = 0, KB_H = 0;

// ── helpers ───────────────────────────────────────────────────────────────────
static void loadKeymap() {
    for (const auto& k : layout())
        try { g_keymap[k.row][k.col] = viaGet(g_dev, g_layer, k.row, k.col); } catch (...) {}
}

static void loadAll() {
    try { g_global = getGlobal(g_dev); } catch (...) {}
    try { g_feat   = getFeat(g_dev);   } catch (...) {}
    for (int i = 0; i < TD_SLOT_COUNT; i++) try { g_td[i]  = getTd(g_dev, i);  } catch (...) {}
    for (int i = 0; i < INDICATOR_COUNT; i++) try { g_ind[i] = getInd(g_dev, i); } catch (...) {}
    for (int i = 0; i < COMBO_SLOT_COUNT; i++) try { g_combo[i] = getCombo(g_dev, i); } catch (...) {}
    for (int i = 0; i < KO_SLOT_COUNT;    i++) try { g_ko[i]    = getKo(g_dev, i);    } catch (...) {}
    loadKeymap();
}

static bool KcPicker(const char* id, uint16_t& kc);  // fwd
static bool KcBuilder(uint16_t& kc);                  // fwd

// Tabbed category grid — call inside an open popup. Sets kc and returns true
// (and closes the popup) when a keycode button is clicked. A filter box at the
// top narrows the grid (within the active tab) by case-insensitive substring.
// A final "Build" tab composes Mod-Tap / Layer-Tap / One-Shot / modded keycodes.
static bool KcPickerBody(uint16_t& kc) {
    static char filter[32] = "";
    auto low = [](std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32; return s; };

    if (ImGui::IsWindowAppearing()) { filter[0] = 0; ImGui::SetKeyboardFocusHere(); }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##kcfilter", "filter (e.g. vol, rgb, td)", filter, sizeof(filter));
    std::string f = low(filter);

    bool changed = false;
    if (ImGui::BeginTabBar("##cats")) {
        for (const auto& cat : keycodeCategories()) {
            if (ImGui::BeginTabItem(cat.name.c_str())) {
                ImGui::BeginChild("##grid", ImVec2(0, 270));
                std::vector<const std::pair<std::string, uint16_t>*> vis;
                for (const auto& e : cat.entries)
                    if (f.empty() || low(e.first).find(f) != std::string::npos) vis.push_back(&e);

                float right   = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
                float spacing = ImGui::GetStyle().ItemSpacing.x;
                float padX    = ImGui::GetStyle().FramePadding.x * 2;
                for (size_t i = 0; i < vis.size(); i++) {
                    bool sel = vis[i]->second == kc;
                    if (sel) { ImGui::PushStyleColor(ImGuiCol_Button, SELECTED_BTN);
                               ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f,0.05f,0.10f,1.0f)); }
                    ImGui::PushID((int)i);
                    if (ImGui::Button(vis[i]->first.c_str())) {
                        kc = vis[i]->second; changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                    if (sel) ImGui::PopStyleColor(2);
                    // Wrap manually: stay on the row only if the next button fits.
                    if (i + 1 < vis.size()) {
                        float nextW = ImGui::CalcTextSize(vis[i + 1]->first.c_str()).x + padX;
                        if (ImGui::GetItemRectMax().x + spacing + nextW < right)
                            ImGui::SameLine();
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        if (ImGui::BeginTabItem("Build")) {
            ImGui::BeginChild("##build", ImVec2(0, 270));
            changed |= KcBuilder(kc);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    return changed;
}

// Inline keycode picker: a button showing the current name that opens the
// tabbed popup. Returns true when a new code is chosen.
static bool KcPicker(const char* id, uint16_t& kc) {
    bool changed = false;
    float w = ImGui::CalcItemWidth();
    std::string label = nameOf(kc) + "##btn" + id;
    if (ImGui::Button(label.c_str(), ImVec2(w, 0)))
        ImGui::OpenPopup(id);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(540, 380), ImGuiCond_Appearing);
    // Round this popup's corners. Global PopupRounding is forced to 0 for viewports
    // (so edge-spawned dropdowns don't show desktop in rounded corners); this popup
    // always fits centered inside the main window, so rounding it is safe.
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
    if (ImGui::BeginPopup(id)) {
        changed = KcPickerBody(kc);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    return changed;
}

// Composes a Mod-Tap / Layer-Tap / One-Shot-Mod / modded keycode from a base
// key + modifiers (or a layer). Returns true (and sets kc) when "Use" is clicked.
static bool KcBuilder(uint16_t& kc) {
    static int      mode  = 1;        // 0 plain,1 MT,2 LT,3 OSM,4 modded
    static bool     m[4]  = {};       // Ctrl, Shift, Alt, Gui
    static bool     right = false;    // right-hand mods
    static int      layer = 0;
    static uint16_t base  = 0x04;     // KC_A

    const char* modes[] = {"Plain", "Mod-Tap (MT)", "Layer-Tap (LT)",
                           "One-Shot Mod (OSM)", "Modded (Ctrl/Shift/…)"};
    ImGui::SetNextItemWidth(220);
    ImGui::Combo("Mode", &mode, modes, 5);

    bool useMods = (mode == 1 || mode == 3 || mode == 4);
    bool useBase = (mode != 3);

    if (useMods) {
        const char* mn[4] = {"Ctrl", "Shift", "Alt", "Gui"};
        for (int i = 0; i < 4; i++) { ImGui::Checkbox(mn[i], &m[i]); ImGui::SameLine(); }
        ImGui::Checkbox("Right-hand", &right);
    }
    if (mode == 2) { ImGui::SetNextItemWidth(220); ImGui::SliderInt("Layer", &layer, 0, 15); }
    if (useBase) {
        ImGui::TextUnformatted("Base key"); ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        KcPicker("##buildbase", base);
    }

    uint8_t  mods = (uint8_t)((m[0] ? 1 : 0) | (m[1] ? 2 : 0) | (m[2] ? 4 : 0) |
                              (m[3] ? 8 : 0) | (right ? 0x10 : 0));
    uint16_t result;
    switch (mode) {
        case 1:  result = (uint16_t)(QK_MOD_TAP   | (mods << 8) | (base & 0xFF)); break;
        case 2:  result = (uint16_t)(QK_LAYER_TAP | ((layer & 0xF) << 8) | (base & 0xFF)); break;
        case 3:  result = (uint16_t)(QK_OSM       | (mods & 0x1F)); break;
        case 4:  result = (uint16_t)(QK_MODS      | (mods << 8) | (base & 0xFF)); break;
        default: result = base; break;
    }

    ImGui::Separator();
    ImGui::Text("Result: %s", nameOf(result).c_str());
    bool applied = false;
    pushPrimaryButtonStyle();
    if (ImGui::Button("Use this keycode")) {
        kc = result; applied = true;
        ImGui::CloseCurrentPopup();
    }
    popPrimaryButtonStyle();
    return applied;
}

// ── keyboard panel ────────────────────────────────────────────────────────────
static void drawKeyboard() {
    ImGui::SeparatorText("Keyboard");

    // layer buttons
    for (int i = 0; i < 4; i++) {
        if (i > 0) ImGui::SameLine();
        bool on = (g_layer == i);
        pushPrimaryButtonStyle();
        if (on) { ImGui::PushStyleColor(ImGuiCol_Button, SELECTED_BTN);
                   ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f,0.05f,0.10f,1.0f)); }
        if (ImGui::SmallButton(LAYER_NAMES[i])) {
            g_layer = i;
            try { loadKeymap(); } catch (...) {}
        }
        if (on) ImGui::PopStyleColor(2);
        popPrimaryButtonStyle();
    }

    ImGui::SameLine(0, 20);
    pushPrimaryButtonStyle();
    if (g_identifying) {
        ImGui::TextDisabled("Press a key on the keyboard...");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) g_identifying = false;
    } else {
        if (ImGui::SmallButton("Identify a key")) {
            try {
                startIdentify(g_dev);
                g_identifying = true;
                g_identTimeout = 15.0f;
            } catch (...) {}
        }
    }
    popPrimaryButtonStyle();

    // Scale key unit to fill available width, leaving an equal margin on each side.
    // The child gets zero window padding so the margin is controlled here only.
    const float MARGIN = 8.0f;
    float availW = ImGui::GetContentRegionAvail().x;
    float ku     = (availW - 2 * MARGIN) / KB_W;
    float canvasW = availW;
    float canvasH = KB_H * ku + 2 * MARGIN;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::BeginChild("##kb", {canvasW, canvasH}, ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImVec2 orig = ImGui::GetCursorScreenPos();
        orig.x += MARGIN; orig.y += MARGIN;
        ImGui::Dummy({canvasW, canvasH});

        auto* dl  = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        bool winHovered = ImGui::IsWindowHovered();

        for (const auto& k : layout()) {
            float x = orig.x + k.x * ku;
            float y = orig.y + k.y * ku;
            float w = k.w * ku - KEY_GAP;
            float h = ku - KEY_GAP;

            bool hl = g_identHighlight > 0 && k.row == g_identHlRow && k.col == g_identHlCol;
            ImVec2 p0{x, y}, p1{x + w, y + h};
            bool hov = winHovered && !g_identifying &&
                       io.MousePos.x >= p0.x && io.MousePos.x < p1.x &&
                       io.MousePos.y >= p0.y && io.MousePos.y < p1.y;

            ImU32 fill = hl  ? IM_COL32(210,170,0,255)
                       : hov ? IM_COL32(74,78,89,255)
                              : IM_COL32(58,61,70,255);
            dl->AddRectFilled(p0, p1, fill, 4.0f);
            dl->AddRect(p0, p1, IM_COL32(28,30,36,255), 4.0f);
            if (hov)
                dl->AddRect(p0, p1, IM_COL32(51,102,204,255), 4.0f, 0, 2.0f);

            if (hov && ImGui::IsMouseClicked(0)) {
                g_editorRow = k.row; g_editorCol = k.col;
                g_editorKc  = g_keymap[k.row][k.col];
                g_editorOpen = true;
            }

            // fit label
            uint16_t kc = g_keymap[k.row][k.col];
            std::string label = nameOf(kc);
            while (label.size() > 1 && ImGui::CalcTextSize(label.c_str()).x > w - 4)
                label.pop_back();

            ImU32 tc = hl ? IM_COL32(0,0,0,255) : IM_COL32(205,210,219,255);
            ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            dl->AddText({x + (w - ts.x) * 0.5f, y + (h - ts.y) * 0.5f}, tc, label.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // key editor popup — pick a keycode from the tabbed grid; applies on select
    if (g_editorOpen) { ImGui::OpenPopup("Key Editor"); g_editorOpen = false; }
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(540, 380), ImGuiCond_Appearing);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);  // see note in KcPicker
    if (ImGui::BeginPopup("Key Editor")) {
        ImGui::Text("R%d C%d  (layer %d)  —  %s",
                    g_editorRow, g_editorCol, g_layer, nameOf(g_editorKc).c_str());
        if (KcPickerBody(g_editorKc)) {
            try {
                viaSet(g_dev, g_layer, g_editorRow, g_editorCol, g_editorKc);
                g_keymap[g_editorRow][g_editorCol] = g_editorKc;
            } catch (...) {}
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    // Debounce settings — shown below the keyboard
    ImGui::SeparatorText("Debounce");
    ImGui::BeginDisabled(g_identifying);

    ImGui::TextUnformatted("Time");
    ImGui::SameLine();
    {
        int v = (int)g_feat.debounce;
        ImGui::SetNextItemWidth(200);
        bool ch = ImGui::SliderInt("##dbt", &v, 0, 50);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(72);
        ch |= ImGui::InputInt("ms##dbt", &v, 0, 0);
        if (ch) {
            v = std::clamp(v, 0, 50);
            g_feat.debounce = (uint16_t)v;
            try { setParam(g_dev, 3, g_feat.debounce); } catch (...) {}
        }
    }

    ImGui::TextUnformatted("Method");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    {
        uint8_t m = g_feat.debounceMethod < 4 ? g_feat.debounceMethod : 0;
        if (ImGui::BeginCombo("##dbmethod", DB_METHOD_LABELS[m])) {
            for (uint8_t i = 0; i < 4; i++) {
                bool sel = (g_feat.debounceMethod == i);
                if (ImGui::Selectable(DB_METHOD_LABELS[i], sel)) {
                    g_feat.debounceMethod = i;
                    try { setParam(g_dev, 4, i); } catch (...) {}
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::EndDisabled();
}

// ── tap-dance panel ───────────────────────────────────────────────────────────
static void drawTapDance() {
    ImGui::SeparatorText("Tap Dance Slots");
    ImGui::TextDisabled("Disabled slots act as their plain tap key. Tap term 0 = inherit global. PH overrides permissive hold for that slot.");

    int n = g_showAllTd ? TD_SLOT_COUNT : 8;
    // Size the table to its content: a ScrollX table with outer_size.y==0 fills all
    // available vertical space, leaving empty rows + a scrollbar below the data.
    // A data row is frame-height + cell padding; the header is plain text + cell
    // padding (no frame). Only reserve the horizontal scrollbar when the fixed
    // columns actually overflow the available width, else it becomes dead space.
    const ImGuiStyle& st = ImGui::GetStyle();
    const float headerH = ImGui::GetTextLineHeight() + st.CellPadding.y * 2.0f;
    const float rowH    = ImGui::GetFrameHeight()    + st.CellPadding.y * 2.0f;
    const float colsW   = (28 + 50 + 145 + 145 + 95 + 68 + 95 + 52)
                        + st.CellPadding.x * 2.0f * 8;   // inner cell padding, 8 cols
    float tdH = headerH + rowH * n + 2.0f;               // rows + outer borders
    if (ImGui::GetContentRegionAvail().x < colsW + 4.0f)
        tdH += st.ScrollbarSize;
    const ImVec2 tdOuter(0.0f, tdH);
    if (ImGui::BeginTable("td", 8,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX,
            tdOuter)) {
        ImGui::TableSetupColumn("#",         ImGuiTableColumnFlags_WidthFixed,  28);
        ImGui::TableSetupColumn("Name",      ImGuiTableColumnFlags_WidthFixed,  50);
        ImGui::TableSetupColumn("Tap",       ImGuiTableColumnFlags_WidthFixed, 145);
        ImGui::TableSetupColumn("Secondary", ImGuiTableColumnFlags_WidthFixed, 145);
        ImGui::TableSetupColumn("Mode",      ImGuiTableColumnFlags_WidthFixed,  95);
        ImGui::TableSetupColumn("Tap term",  ImGuiTableColumnFlags_WidthFixed,  68);
        ImGui::TableSetupColumn("PH",        ImGuiTableColumnFlags_WidthFixed,  95);
        ImGui::TableSetupColumn("Enabled",   ImGuiTableColumnFlags_WidthFixed,  52);
        ImGui::TableHeadersRow();

        for (int i = 0; i < n; i++) {
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", i);

            ImGui::TableSetColumnIndex(1);
            std::string nm = "TD" + std::to_string(i);
            ImGui::TextUnformatted(nm.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1);
            if (KcPicker("##tap", g_td[i].tap))
                try { setTdKc(g_dev, i, g_td[i].tap, g_td[i].sec); } catch (...) {}

            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-1);
            if (KcPicker("##sec", g_td[i].sec))
                try { setTdKc(g_dev, i, g_td[i].tap, g_td[i].sec); } catch (...) {}

            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-1);
            const char* modes[] = {"double","hold"};
            int mode = g_td[i].mode;
            if (ImGui::Combo("##mode", &mode, modes, 2)) {
                g_td[i].mode = (uint8_t)mode;
                try { setTdMode(g_dev, i, g_td[i].mode); } catch (...) {}
            }

            ImGui::TableSetColumnIndex(5);
            ImGui::SetNextItemWidth(-1);
            int tt = (int)g_td[i].tappingTerm;
            if (ImGui::InputInt("##tt", &tt, 0, 0)) {
                if (tt < 0) tt = 0; if (tt > 5000) tt = 5000;
                g_td[i].tappingTerm = (uint16_t)tt;
                try { setTdTiming(g_dev, i, g_td[i].tappingTerm, g_td[i].phFlags); } catch (...) {}
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = use global tapping term");

            ImGui::TableSetColumnIndex(6);
            ImGui::SetNextItemWidth(-1);
            const char* phLabels[] = {"Global","On","Off"};
            int phMode = (g_td[i].phFlags & TD_HAS_PH)
                         ? ((g_td[i].phFlags & TD_PH_VALUE) ? 1 : 2) : 0;
            if (ImGui::Combo("##ph", &phMode, phLabels, 3)) {
                if      (phMode == 0) g_td[i].phFlags = 0;
                else if (phMode == 1) g_td[i].phFlags = TD_HAS_PH | TD_PH_VALUE;
                else                  g_td[i].phFlags = TD_HAS_PH;
                try { setTdTiming(g_dev, i, g_td[i].tappingTerm, g_td[i].phFlags); } catch (...) {}
            }

            ImGui::TableSetColumnIndex(7);
            bool en = g_td[i].enabled;
            if (ImGui::Checkbox("##en", &en)) {
                g_td[i].enabled = en;
                try { setTdEn(g_dev, i, en); } catch (...) {}
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    pushPrimaryButtonStyle();
    if (ImGui::SmallButton(g_showAllTd ? "Show first 8 slots" : "Show all 32 slots"))
        g_showAllTd = !g_showAllTd;
    popPrimaryButtonStyle();
}

// ── timing panel ──────────────────────────────────────────────────────────────
static void drawTiming() {
    ImGui::SeparatorText("Timing");

    struct P { const char* label; uint16_t* val; int lo, hi; uint8_t pid; bool isTT; };
    static const P params[] = {
        {"Tapping term",       &g_global.tt,         50,  500, 0, true },
        {"Quick tap term",     &g_feat.quicktap,      0,  500, 0, false},
        {"Auto-shift timeout", &g_feat.astimeout,    50,  500, 1, false},
        {"Caps Word timeout",  &g_feat.cwtimeout,     0,10000, 2, false},
        {"One-shot timeout",   &g_feat.oneshotTimeout,0, 5000, 5, false},
    };

    float startX  = ImGui::GetCursorPosX();
    float sliderX = startX + ImGui::CalcTextSize("Auto-shift timeout").x + ImGui::GetStyle().ItemSpacing.x;

    for (auto& p : params) {
        ImGui::PushID(p.label);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(p.label);
        ImGui::SameLine(sliderX);
        int v = (int)*p.val;
        ImGui::SetNextItemWidth(200);
        bool ch = ImGui::SliderInt("##sl", &v, p.lo, p.hi);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(72);
        ch |= ImGui::InputInt("ms", &v, 0, 0);
        if (ch) {
            v = std::clamp(v, p.lo, p.hi);
            *p.val = (uint16_t)v;
            try { if (p.isTT) setTT(g_dev, *p.val); else setParam(g_dev, p.pid, *p.val); }
            catch (...) {}
        }
        ImGui::PopID();
    }
}

// ── features panel (runtime feature-flag toggles) ──────────────────────────────
static void drawFeatures() {
    ImGui::SeparatorText("Features");

    auto flagBox = [&](int bit, const char* label) {
        bool v = !!(g_feat.flags & (1 << bit));
        if (ImGui::Checkbox(label, &v)) {
            if (v) g_feat.flags |=  (uint16_t)(1 << bit);
            else   g_feat.flags &= ~(uint16_t)(1 << bit);
            try { setFlag(g_dev, bit, v); } catch (...) {}
        }
    };

    // Caps Word + its shift-activation sub-options, grouped together.
    flagBox(0, FLAG_NAMES[0]);  // Caps Word (master enable)
    if (g_feat.flags & (1 << 0)) {
        ImGui::Indent(20.0f);
        flagBox(5, "Double-tap Shift -> Caps Word");
        flagBox(6, "Both Shifts -> Caps Word");
        ImGui::Unindent(20.0f);
    }
    flagBox(1, FLAG_NAMES[1]);  // Permissive hold
    flagBox(2, FLAG_NAMES[2]);  // Hold on other key press
    flagBox(3, FLAG_NAMES[3]);  // Retro tapping
    flagBox(4, FLAG_NAMES[4]);  // Auto Shift
}

// ── combos panel ──────────────────────────────────────────────────────────────
static void drawCombos() {
    ImGui::SeparatorText("Combos");
    ImGui::TextDisabled("Press all input keys together to emit the output. Needs at least two inputs.");

    if (ImGui::BeginTable("combos", COMBO_MAX_KEYS + 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28);
        for (int k = 0; k < COMBO_MAX_KEYS; k++) {
            char h[8]; snprintf(h, sizeof(h), "Key %d", k + 1);
            ImGui::TableSetupColumn(h, ImGuiTableColumnFlags_WidthFixed, 110);
        }
        ImGui::TableSetupColumn("Output",  ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();

        for (int i = 0; i < COMBO_SLOT_COUNT; i++) {
            ImGui::TableNextRow();
            ImGui::PushID(i);
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", i);

            bool dirty = false;
            for (int k = 0; k < COMBO_MAX_KEYS; k++) {
                ImGui::TableSetColumnIndex(1 + k);
                ImGui::SetNextItemWidth(-1);
                ImGui::PushID(k);
                if (KcPicker("##ck", g_combo[i].keys[k])) dirty = true;
                ImGui::PopID();
            }
            ImGui::TableSetColumnIndex(1 + COMBO_MAX_KEYS);
            ImGui::SetNextItemWidth(-1);
            if (KcPicker("##co", g_combo[i].output)) dirty = true;

            ImGui::TableSetColumnIndex(2 + COMBO_MAX_KEYS);
            bool en = g_combo[i].enabled;
            if (ImGui::Checkbox("##en", &en)) { g_combo[i].enabled = en; dirty = true; }

            if (dirty) try { setCombo(g_dev, i, g_combo[i]); } catch (...) {}
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ── key overrides panel ─────────────────────────────────────────────────────────
// Renders a compact 8-bit modifier-mask editor (LCtl..RGui). Returns true on change.
static bool ModMaskEdit(const char* id, uint8_t& mask) {
    bool changed = false;
    const char* const* names = modMaskNames();
    ImGui::PushID(id);
    for (int b = 0; b < 8; b++) {
        bool on = mask & (1 << b);
        if (ImGui::Checkbox(names[b], &on)) {
            if (on) mask |= (uint8_t)(1 << b); else mask &= (uint8_t)~(1 << b);
            changed = true;
        }
        if (b != 3 && b != 7) ImGui::SameLine();
    }
    ImGui::PopID();
    return changed;
}

static void drawKeyOverrides() {
    ImGui::SeparatorText("Key Overrides");
    ImGui::TextDisabled("When trigger + trigger-mods are held (and negative-mods are not), send replacement instead.");

    static int sel = 0;
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("Slot", &sel, 0, KO_SLOT_COUNT - 1);
    if (sel < 0) sel = 0; if (sel >= KO_SLOT_COUNT) sel = KO_SLOT_COUNT - 1;
    KeyOverride& k = g_ko[sel];
    bool dirty = false;

    if (ImGui::Checkbox("Enabled", &k.enabled)) dirty = true;

    ImGui::TextUnformatted("Trigger"); ImGui::SameLine(140);
    ImGui::SetNextItemWidth(180); if (KcPicker("##kotrig", k.trigger)) dirty = true;

    ImGui::TextUnformatted("Replacement"); ImGui::SameLine(140);
    ImGui::SetNextItemWidth(180); if (KcPicker("##korepl", k.replacement)) dirty = true;

    ImGui::TextUnformatted("Trigger mods");    if (ModMaskEdit("tm", k.triggerMods))    dirty = true;
    ImGui::TextUnformatted("Suppressed mods"); if (ModMaskEdit("sm", k.suppressedMods)) dirty = true;
    ImGui::TextUnformatted("Negative mods");   if (ModMaskEdit("nm", k.negativeMods))   dirty = true;

    ImGui::TextUnformatted("Layers"); ImGui::SameLine(140);
    for (int l = 0; l < 4; l++) {
        bool on = k.layers & (1 << l);
        char lbl[8]; snprintf(lbl, sizeof(lbl), "%d", l);
        if (ImGui::Checkbox(lbl, &on)) {
            if (on) k.layers |= (uint8_t)(1 << l); else k.layers &= (uint8_t)~(1 << l);
            dirty = true;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    if (dirty) {
        if (k.options == 0) k.options = 0x07;  // ko_options_all_activations default
        try { setKo(g_dev, sel, k); } catch (...) {}
    }
}

// ── indicators panel ──────────────────────────────────────────────────────────
static void drawIndicators() {
    ImGui::SeparatorText("Indicators");

    // Fixed column so all color pickers line up after the longest label
    float colX = ImGui::GetFrameHeight()
               + ImGui::GetStyle().ItemInnerSpacing.x
               + ImGui::CalcTextSize("Windows mode").x
               + ImGui::GetStyle().ItemSpacing.x;
    // Swatch + single hex field "#RRGGBB"
    float colW = ImGui::GetFrameHeight()
               + ImGui::GetStyle().ItemInnerSpacing.x
               + ImGui::CalcTextSize("#FFFFFF").x
               + ImGui::GetStyle().FramePadding.x * 2;

    for (int i = 0; i < INDICATOR_COUNT; i++) {
        ImGui::PushID(i);
        bool en = g_ind[i].enabled;
        if (ImGui::Checkbox(IND_NAMES[i], &en)) {
            g_ind[i].enabled = en;
            try { setInd(g_dev, i, en, g_ind[i].r, g_ind[i].g, g_ind[i].b); } catch (...) {}
        }
        ImGui::SameLine(colX);
        ImGui::SetNextItemWidth(colW);
        float col[3] = {g_ind[i].r/255.0f, g_ind[i].g/255.0f, g_ind[i].b/255.0f};
        if (ImGui::ColorEdit3("##col", col, ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_DisplayHex)) {
            g_ind[i].r = (uint8_t)(col[0]*255);
            g_ind[i].g = (uint8_t)(col[1]*255);
            g_ind[i].b = (uint8_t)(col[2]*255);
            try { setInd(g_dev, i, g_ind[i].enabled, g_ind[i].r, g_ind[i].g, g_ind[i].b); } catch (...) {}
        }
        ImGui::PopID();
    }
}

// ── presets panel ─────────────────────────────────────────────────────────────
static void drawPresets() {
    ImGui::SeparatorText("Presets");

    pushPrimaryButtonStyle();
    if (ImGui::Button("Save preset...")) {
        NFD::UniquePath path;
        nfdfilteritem_t f[] = {{"JSON Preset","json"}};
        if (NFD::SaveDialog(path, f, 1, nullptr, "preset.json") == NFD_OKAY) {
            try {
                auto cfg = readConfig(g_dev);
                std::ofstream out(path.get());
                out << cfg.dump(2) << "\n";
            } catch (...) {}
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load preset...")) {
        NFD::UniquePath path;
        nfdfilteritem_t f[] = {{"JSON Preset","json"}};
        if (NFD::OpenDialog(path, f, 1) == NFD_OKAY) {
            try {
                std::ifstream in(path.get());
                nlohmann::json p; in >> p;
                writeConfig(g_dev, p);
                loadAll();
            } catch (...) {}
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset config")) {
        try { resetCfg(g_dev); loadAll(); } catch (...) {}
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset keymap")) {
        try { viaKmReset(g_dev); loadKeymap(); } catch (...) {}
    }
    popPrimaryButtonStyle();
}

// ── styling ───────────────────────────────────────────────────────────────────
static void setupStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 10.0f;
    style.ChildRounding     = 10.0f;
    style.FrameRounding     = 8.0f;
    style.GrabRounding      = 8.0f;
    style.TabRounding       = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.PopupRounding     = 8.0f;
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.FramePadding      = ImVec2(10.0f, 7.0f);
    style.ItemSpacing       = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    style.WindowPadding     = ImVec2(12.0f, 12.0f);
    style.IndentSpacing     = 20.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                 = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.56f, 0.61f, 0.68f, 1.00f);
    colors[ImGuiCol_WindowBg]             = ImVec4(0.08f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.10f, 0.12f, 0.16f, 0.98f);
    colors[ImGuiCol_Border]               = ImVec4(0.20f, 0.24f, 0.30f, 1.00f);
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.12f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.16f, 0.22f, 0.30f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.20f, 0.28f, 0.38f, 1.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.09f, 0.11f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.11f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.06f, 0.07f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.25f, 0.31f, 0.39f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.40f, 0.50f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.38f, 0.48f, 0.60f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.42f, 0.76f, 0.96f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.42f, 0.76f, 0.96f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.60f, 0.86f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.17f, 0.22f, 0.29f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.24f, 0.31f, 0.40f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.30f, 0.40f, 0.51f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.16f, 0.23f, 0.32f, 0.90f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.23f, 0.33f, 0.44f, 0.95f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.28f, 0.40f, 0.54f, 1.00f);
    colors[ImGuiCol_Separator]            = ImVec4(0.22f, 0.26f, 0.33f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.35f, 0.58f, 0.88f, 1.00f);
    colors[ImGuiCol_SeparatorActive]      = ImVec4(0.42f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0.27f, 0.34f, 0.43f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.35f, 0.58f, 0.88f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.42f, 0.70f, 1.00f, 0.95f);
    colors[ImGuiCol_Tab]                  = ImVec4(0.12f, 0.16f, 0.22f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.20f, 0.29f, 0.40f, 1.00f);
    colors[ImGuiCol_TabActive]            = ImVec4(0.17f, 0.24f, 0.33f, 1.00f);
    colors[ImGuiCol_TabUnfocused]         = ImVec4(0.09f, 0.12f, 0.17f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.13f, 0.18f, 0.25f, 1.00f);
    colors[ImGuiCol_PlotLines]            = ImVec4(0.70f, 0.74f, 0.82f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]     = ImVec4(0.98f, 0.65f, 0.24f, 1.00f);
    colors[ImGuiCol_PlotHistogram]        = ImVec4(0.36f, 0.72f, 0.94f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.98f, 0.65f, 0.24f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.29f, 0.50f, 0.79f, 0.45f);
    colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.04f, 0.05f, 0.08f, 0.74f);

    // When popups float as their own OS windows, transparent/rounded backgrounds
    // would show the desktop behind them — make detached windows opaque & square.
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding              = 0.0f;
        style.PopupRounding               = 0.0f;
        colors[ImGuiCol_WindowBg].w       = 1.0f;
        colors[ImGuiCol_PopupBg].w        = 1.0f;
    }
}

// Center the window on the monitor under the mouse cursor (falls back to the
// primary monitor if the cursor position can't be determined).
static void centerWindowOnCursorMonitor(GLFWwindow* win) {
    int winW, winH;
    glfwGetWindowSize(win, &winW, &winH);

    int cx = 0, cy = 0;
    bool haveCursor = false;
#ifdef _WIN32
    POINT p;
    if (GetCursorPos(&p)) { cx = p.x; cy = p.y; haveCursor = true; }
#endif

    int count = 0;
    GLFWmonitor** mons = glfwGetMonitors(&count);
    GLFWmonitor* target = glfwGetPrimaryMonitor();
    if (haveCursor) {
        for (int i = 0; i < count; i++) {
            int mx, my;
            glfwGetMonitorPos(mons[i], &mx, &my);
            const GLFWvidmode* vm = glfwGetVideoMode(mons[i]);
            if (cx >= mx && cx < mx + vm->width && cy >= my && cy < my + vm->height) {
                target = mons[i];
                break;
            }
        }
    }
    if (!target) return;

    int wx, wy, ww, wh;
    glfwGetMonitorWorkarea(target, &wx, &wy, &ww, &wh);
    glfwSetWindowPos(win, wx + (ww - winW) / 2, wy + (wh - winH) / 2);
}

// Custom (non-OS) title bar drawn at the top of the root window. Returns its
// height. Handles drag-to-move plus minimize/close; the window is fixed-size so
// there is no maximize/resize. Glyphs are drawn by hand (the bundled font has no
// window-control symbols).
static float drawTitleBar(GLFWwindow* win) {
    const float barH = ImGui::GetFrameHeight() + 6.0f;
    const float btnW = barH;
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImVec2 p0       = ImGui::GetCursorScreenPos();
    float  fullW    = ImGui::GetContentRegionAvail().x;
    ImVec2 p1       = ImVec2(p0.x + fullW, p0.y + barH);

    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(0.09f, 0.11f, 0.15f, 1.0f)));
    dl->AddLine(ImVec2(p0.x, p1.y - 1.0f), ImVec2(p1.x, p1.y - 1.0f),
                ImGui::GetColorU32(ImGuiCol_Border));

    const char* title = "Keychron Q1 Pro Config";
    ImVec2 ts = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(p0.x + 12.0f, p0.y + (barH - ts.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), title);

    // Window buttons (minimize, close) anchored to the right.
    auto winButton = [&](const char* id, bool isClose) -> bool {
        ImVec2 bp = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton(id, ImVec2(btnW, barH));
        if (ImGui::IsItemHovered()) {
            ImU32 c = isClose ? ImGui::GetColorU32(ImVec4(0.86f, 0.22f, 0.27f, 1.0f))
                              : ImGui::GetColorU32(ImVec4(0.24f, 0.31f, 0.40f, 1.0f));
            dl->AddRectFilled(bp, ImVec2(bp.x + btnW, bp.y + barH), c);
        }
        ImVec2 c = ImVec2(bp.x + btnW * 0.5f, bp.y + barH * 0.5f);
        ImU32  g = ImGui::GetColorU32(ImGuiCol_Text);
        float  r = 4.0f;
        if (isClose) {
            dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), g, 1.5f);
            dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), g, 1.5f);
        } else {
            dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y + r), g, 1.5f);
        }
        return clicked;
    };
    ImGui::SetCursorScreenPos(ImVec2(p1.x - btnW * 2.0f, p0.y));
    if (winButton("##min", false)) glfwIconifyWindow(win);
    ImGui::SameLine(0.0f, 0.0f);
    if (winButton("##close", true)) glfwSetWindowShouldClose(win, GLFW_TRUE);

    // Drag region: the bar minus the buttons. Move the window frame-relatively —
    // after each move the window-relative cursor returns toward the grab point.
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##titlebar_drag", ImVec2(fullW - btnW * 2.0f, barH));
    static bool   dragging = false;
    static double startX = 0.0, startY = 0.0;
    if (ImGui::IsItemActivated()) {
        dragging = true;
        glfwGetCursorPos(win, &startX, &startY);
    }
    if (dragging) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            dragging = false;
        } else {
            double mx, my; glfwGetCursorPos(win, &mx, &my);
            int wx, wy;    glfwGetWindowPos(win, &wx, &wy);
            glfwSetWindowPos(win, wx + (int)(mx - startX), wy + (int)(my - startY));
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
    return barH;
}

// ── main entry ────────────────────────────────────────────────────────────────
int gui_main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);    // show after positioning to avoid a jump
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);  // no OS title bar — we draw our own
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);  // fixed-size window
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    // Height accounts for the custom title bar + content margins (the OS frame
    // used to sit outside the client area; ours lives inside it).
    GLFWwindow* win = glfwCreateWindow(900, 800, "Keychron Q1 Pro Config", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
#ifdef _WIN32
    // Round the borderless window's corners (Windows 11; harmless no-op on Win10).
    // GLFW_DECORATED is off, so DWM won't round it automatically — opt in explicitly.
    {
        HWND hwnd = glfwGetWin32Window(win);
        DWORD pref = 2 /* DWMWCP_ROUND */;
        DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                              &pref, sizeof(pref));
    }
#endif
    centerWindowOnCursorMonitor(win);
    glfwShowWindow(win);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // picker can float as its own OS window
    setupStyle();
    ImFontConfig fontCfg;
    fontCfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(
        const_cast<uint8_t*>(kInterRegular_data),
        static_cast<int>(kInterRegular_size),
        16.0f, &fontCfg);
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // pre-compute keyboard extents
    for (const auto& k : layout()) {
        KB_W = std::max(KB_W, k.x + k.w);
        KB_H = std::max(KB_H, k.y + 1.0f);
    }

    NFD::Guard nfd;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        // identify: poll for unsolicited report each frame
        if (g_identifying) {
            g_identTimeout -= io.DeltaTime;
            if (g_identTimeout <= 0 || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                g_identifying = false;
            } else {
                std::array<uint8_t, 32> rep;
                if (g_dev.tryRead(rep) && rep[0] == 0xAC && rep[1] == 0x08) {
                    g_identHlRow  = rep[2]; g_identHlCol = rep[3];
                    g_identHighlight = 1.5f;
                    g_identifying = false;
                }
            }
        }
        if (g_identHighlight > 0) g_identHighlight -= io.DeltaTime;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Pin the root window to the main viewport. With multi-viewport enabled,
        // window positions are global desktop coordinates, so {0,0} would land at
        // the monitor's upper-left instead of the app window — use the viewport's
        // actual position/size and bind the root window to it.
        const ImGuiViewport* mainVp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainVp->WorkPos);
        ImGui::SetNextWindowSize(mainVp->WorkSize);
        ImGui::SetNextWindowViewport(mainVp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("##root", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(2);

        // Custom title bar (replaces the OS one), then a padded content region
        // inset from the window edges on all sides.
        drawTitleBar(win);
        // AlwaysUseWindowPadding: borderless child windows ignore WindowPadding
        // otherwise, leaving content flush against the edges.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();

        // connection bar
        pushPrimaryButtonStyle();
        if (!g_connected) {
            if (ImGui::Button("Connect")) {
                if (g_dev.open()) {
                    g_connected = true;
                    snprintf(g_statusMsg, sizeof(g_statusMsg),
                             "Connected: %s (3434:0610)", g_dev.productName().c_str());
                    loadAll();
                } else {
                    snprintf(g_statusMsg, sizeof(g_statusMsg), "Device not found (VID 3434 PID 0610)");
                }
            }
        } else {
            if (ImGui::Button("Reconnect")) {
                g_dev.close(); g_connected = false;
                if (g_dev.open()) {
                    g_connected = true;
                    snprintf(g_statusMsg, sizeof(g_statusMsg),
                             "Connected: %s (3434:0610)", g_dev.productName().c_str());
                    loadAll();
                }
            }
        }
        popPrimaryButtonStyle();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", g_statusMsg);
        ImGui::Separator();

        if (g_connected) {
            // Reserve space at the bottom for the always-visible Presets section
            float footerH = ImGui::GetTextLineHeightWithSpacing()   // SeparatorText
                          + ImGui::GetFrameHeightWithSpacing()      // button row
                          + ImGui::GetStyle().ItemSpacing.y * 2;
            ImGui::BeginChild("##tabsregion", ImVec2(0, -footerH));
            if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {
                if (ImGui::BeginTabItem("Keyboard")) {
                    ImGui::BeginChild("##tab_kb");
                    drawKeyboard();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                // remaining tabs disabled while identifying to prevent concurrent HID calls
                if (ImGui::BeginTabItem("Features")) {
                    ImGui::BeginChild("##tab_feat");
                    ImGui::BeginDisabled(g_identifying);
                    drawFeatures();
                    ImGui::EndDisabled();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Tap Dance")) {
                    ImGui::BeginChild("##tab_td");
                    ImGui::BeginDisabled(g_identifying);
                    drawTapDance();
                    ImGui::EndDisabled();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Timing")) {
                    ImGui::BeginChild("##tab_timing");
                    ImGui::BeginDisabled(g_identifying);
                    drawTiming();
                    ImGui::EndDisabled();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Combos")) {
                    ImGui::BeginChild("##tab_combo");
                    ImGui::BeginDisabled(g_identifying);
                    drawCombos();
                    ImGui::EndDisabled();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Key Overrides")) {
                    ImGui::BeginChild("##tab_ko");
                    ImGui::BeginDisabled(g_identifying);
                    drawKeyOverrides();
                    ImGui::EndDisabled();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Indicators")) {
                    ImGui::BeginChild("##tab_ind");
                    ImGui::BeginDisabled(g_identifying);
                    drawIndicators();
                    ImGui::EndDisabled();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndChild();

            // Presets: always visible at the bottom, not a tab
            ImGui::Separator();
            ImGui::BeginDisabled(g_identifying);
            drawPresets();
            ImGui::EndDisabled();
        }

        ImGui::EndChild();  // ##content
        ImGui::End();
        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.10f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Render any popups that floated out into their own OS windows.
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
#ifdef _WIN32
            // ImGui forces square corners on viewport-owned OS windows (the picker
            // popups). Round them via DWM, same as the main window (Win11; no-op on 10).
            for (ImGuiViewport* vp : ImGui::GetPlatformIO().Viewports) {
                if (vp->PlatformHandleRaw) {
                    DWORD pref = 2 /* DWMWCP_ROUND */;
                    DwmSetWindowAttribute((HWND)vp->PlatformHandleRaw,
                                          33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                                          &pref, sizeof(pref));
                }
            }
#endif
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup);
        }
        glfwSwapBuffers(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
