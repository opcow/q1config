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
#include <nfd.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include "../fonts/inter_regular.inl"

// ── constants ─────────────────────────────────────────────────────────────────
static constexpr float KEY_U   = 44.0f;
static constexpr float KEY_GAP = 3.0f;

static const char* LAYER_NAMES[] = {"0 MAC_BASE","1 MAC_FN","2 WIN_BASE","3 WIN_FN"};
// Slots are named TD0..TDn so a slot's name is its index (matches the firmware).
static const char* IND_NAMES[]   = {"Caps Lock","Caps Word","WIN_FN layer"};
static const char* FLAG_NAMES[]  = {"Caps Word","Permissive hold","Hold on other key press","Retro tapping","Auto Shift"};

// ── app state ─────────────────────────────────────────────────────────────────
static HidDevice g_dev;
static bool      g_connected = false;
static char      g_statusMsg[128] = "Not connected.";

static int      g_layer = 2;
static uint16_t g_keymap[6][16] = {};

static TdSlot  g_td[TD_SLOT_COUNT]  = {};
static bool    g_showAllTd = false;

static GlobalState g_global = {};
static FeatState   g_feat   = {};
static IndState    g_ind[3] = {};

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
    for (int i = 0; i < 3;  i++) try { g_ind[i] = getInd(g_dev, i); } catch (...) {}
    loadKeymap();
}

// Tabbed category grid — call inside an open popup. Sets kc and returns true
// (and closes the popup) when a keycode button is clicked. A filter box at the
// top narrows the grid (within the active tab) by case-insensitive substring.
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
                    if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
                                 ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    ImGui::PushID((int)i);
                    if (ImGui::Button(vis[i]->first.c_str())) {
                        kc = vis[i]->second; changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                    if (sel) ImGui::PopStyleColor();
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
    ImGui::SetNextWindowSize(ImVec2(540, 380), ImGuiCond_Appearing);
    if (ImGui::BeginPopup(id)) {
        changed = KcPickerBody(kc);
        ImGui::EndPopup();
    }
    return changed;
}

// ── keyboard panel ────────────────────────────────────────────────────────────
static void drawKeyboard() {
    ImGui::SeparatorText("Keyboard");

    // layer buttons
    for (int i = 0; i < 4; i++) {
        if (i > 0) ImGui::SameLine();
        bool on = (g_layer == i);
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(LAYER_NAMES[i])) {
            g_layer = i;
            try { loadKeymap(); } catch (...) {}
        }
        if (on) ImGui::PopStyleColor();
    }

    ImGui::SameLine(0, 20);
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

    // Scale key unit to fill available width, leaving an equal margin on each side.
    // The child gets zero window padding so the margin is controlled here only.
    const float MARGIN = 8.0f;
    float availW = ImGui::GetContentRegionAvail().x;
    float ku     = (availW - 2 * MARGIN) / KB_W;
    float canvasW = availW;
    float canvasH = KB_H * ku + 2 * MARGIN;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::BeginChild("##kb", {canvasW, canvasH}, ImGuiChildFlags_Border,
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
    ImGui::SetNextWindowSize(ImVec2(540, 380), ImGuiCond_Appearing);
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
}

// ── tap-dance panel ───────────────────────────────────────────────────────────
static void drawTapDance() {
    ImGui::SeparatorText("Tap Dance Slots");
    ImGui::TextDisabled("Disabled slots act as their plain tap key. A slot only affects a key assigned TDn.");

    int n = g_showAllTd ? TD_SLOT_COUNT : 8;
    if (ImGui::BeginTable("td", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("#",         ImGuiTableColumnFlags_WidthFixed,   30);
        ImGui::TableSetupColumn("Name",      ImGuiTableColumnFlags_WidthFixed,   60);
        ImGui::TableSetupColumn("Tap",       ImGuiTableColumnFlags_WidthFixed,  170);
        ImGui::TableSetupColumn("Secondary", ImGuiTableColumnFlags_WidthFixed,  170);
        ImGui::TableSetupColumn("Mode",      ImGuiTableColumnFlags_WidthFixed,  100);
        ImGui::TableSetupColumn("Enabled",   ImGuiTableColumnFlags_WidthFixed,   60);
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
            bool en = g_td[i].enabled;
            if (ImGui::Checkbox("##en", &en)) {
                g_td[i].enabled = en;
                try { setTdEn(g_dev, i, en); } catch (...) {}
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::SmallButton(g_showAllTd ? "Show first 8 slots" : "Show all 64 slots"))
        g_showAllTd = !g_showAllTd;
}

// ── timing & features panel ───────────────────────────────────────────────────
static void drawFeatures() {
    ImGui::SeparatorText("Timing & Features");

    struct P { const char* label; uint16_t* val; int lo, hi; uint8_t pid; bool isTT; };
    static const P params[] = {
        {"Tapping term",       &g_global.tt,       50,  500, 0, true },
        {"Quick tap term",     &g_feat.quicktap,    0,  500, 0, false},
        {"Auto-shift timeout", &g_feat.astimeout,  50,  500, 1, false},
        {"Caps Word timeout",  &g_feat.cwtimeout,   0,10000, 2, false},
        {"Debounce",           &g_feat.debounce,    0,   50, 3, false},
    };

    float checkboxX = ImGui::GetCursorPosX() + 200 + ImGui::GetStyle().ItemSpacing.x + 72 + ImGui::GetStyle().ItemSpacing.x + 40;

    // Draw each timing parameter with its corresponding checkbox on the right
    // (first 4 rows pair with flags 0-3; Debounce row 4 has no flag, auto_shift drawn below)
    for (int j = 0; j < 5; j++) {
        auto& p = params[j];
        ImGui::PushID(p.label);
        ImGui::TextUnformatted(p.label);
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

        // Checkbox on the right (only for first 4 flags)
        if (j < 4) {
            ImGui::SameLine(checkboxX);
            bool on = !!(g_feat.flags & (1 << j));
            if (ImGui::Checkbox(FLAG_NAMES[j], &on)) {
                if (on) g_feat.flags |=  (uint16_t)(1 << j);
                else    g_feat.flags &= ~(uint16_t)(1 << j);
                try { setFlag(g_dev, j, on); } catch (...) {}
            }
        }
    }

    // Fifth checkbox (no corresponding timing parameter)
    ImGui::NewLine();
    ImGui::SetCursorPosX(checkboxX);
    bool on = !!(g_feat.flags & (1 << 4));
    if (ImGui::Checkbox(FLAG_NAMES[4], &on)) {
        if (on) g_feat.flags |=  (uint16_t)(1 << 4);
        else    g_feat.flags &= ~(uint16_t)(1 << 4);
        try { setFlag(g_dev, 4, on); } catch (...) {}
    }

    // Debounce method selector
    ImGui::TextUnformatted("Debounce method");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("##dbmethod", dbMethodName(g_feat.debounceMethod))) {
        for (uint8_t i = 0; i < 4; i++) {
            bool sel = (g_feat.debounceMethod == i);
            if (ImGui::Selectable(dbMethodName(i), sel)) {
                g_feat.debounceMethod = i;
                try { setParam(g_dev, 4, i); } catch (...) {}
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

// ── indicators panel ──────────────────────────────────────────────────────────
static void drawIndicators() {
    ImGui::SeparatorText("Indicators");

    // Fixed column so all color pickers line up after the longest label
    float colX = ImGui::GetFrameHeight()
               + ImGui::GetStyle().ItemInnerSpacing.x
               + ImGui::CalcTextSize("WIN_FN layer").x
               + ImGui::GetStyle().ItemSpacing.x;
    // Swatch + single hex field "#RRGGBB"
    float colW = ImGui::GetFrameHeight()
               + ImGui::GetStyle().ItemInnerSpacing.x
               + ImGui::CalcTextSize("#FFFFFF").x
               + ImGui::GetStyle().FramePadding.x * 2;

    for (int i = 0; i < 3; i++) {
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
}

static void pushPrimaryButtonStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.55f, 0.90f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.66f, 1.00f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.14f, 0.44f, 0.78f, 1.0f));
}

static void popPrimaryButtonStyle() {
    ImGui::PopStyleColor(3);
}

// ── main entry ────────────────────────────────────────────────────────────────
int gui_main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* win = glfwCreateWindow(800, 600, "Keychron Q1 Pro Config", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
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

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("##root", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();

        // connection bar
        if (!g_connected) {
            pushPrimaryButtonStyle();
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
            popPrimaryButtonStyle();
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
                if (ImGui::BeginTabItem("Tap Dance")) {
                    ImGui::BeginChild("##tab_td");
                    ImGui::BeginDisabled(g_identifying);
                    drawTapDance();
                    ImGui::EndDisabled();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Timing")) {
                    ImGui::BeginChild("##tab_feat");
                    ImGui::BeginDisabled(g_identifying);
                    drawFeatures();
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

        ImGui::End();
        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.10f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
