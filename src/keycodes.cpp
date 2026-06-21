#include "keycodes.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>

static std::unordered_map<std::string, uint16_t> KC;
static std::vector<std::pair<std::string, uint16_t>> ENTRIES;

static void buildTables() {
    if (!KC.empty()) return;

    KC["NO"] = 0x00; KC["TRNS"] = 0x01;

    const char* alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; alpha[i]; i++) KC[std::string(1, alpha[i])] = (uint16_t)(0x04 + i);

    const char* digits = "123456789";
    for (int i = 0; digits[i]; i++) KC[std::string(1, digits[i])] = (uint16_t)(0x1E + i);

    KC["0"]=0x27; KC["ENT"]=0x28; KC["ESC"]=0x29; KC["BSPC"]=0x2A; KC["TAB"]=0x2B;
    KC["SPC"]=0x2C; KC["MINS"]=0x2D; KC["EQL"]=0x2E; KC["LBRC"]=0x2F; KC["RBRC"]=0x30;
    KC["BSLS"]=0x31; KC["SCLN"]=0x33; KC["QUOT"]=0x34; KC["GRV"]=0x35; KC["COMM"]=0x36;
    KC["DOT"]=0x37; KC["SLSH"]=0x38; KC["CAPS"]=0x39; KC["PSCR"]=0x46; KC["SCRL"]=0x47;
    KC["PAUS"]=0x48; KC["INS"]=0x49; KC["HOME"]=0x4A; KC["PGUP"]=0x4B; KC["DEL"]=0x4C;
    KC["END"]=0x4D; KC["PGDN"]=0x4E; KC["RGHT"]=0x4F; KC["LEFT"]=0x50; KC["DOWN"]=0x51;
    KC["UP"]=0x52; KC["NUM"]=0x53;

    for (int i = 1; i <= 12; i++)  KC["F" + std::to_string(i)] = (uint16_t)(0x3A + (i - 1));
    for (int i = 13; i <= 24; i++) KC["F" + std::to_string(i)] = (uint16_t)(0x68 + (i - 13));

    KC["COLN"]=0x0233; KC["EXLM"]=0x021E; KC["AT"]=0x021F;   KC["HASH"]=0x0220;
    KC["DLR"]=0x0221;  KC["PERC"]=0x0222; KC["CIRC"]=0x0223; KC["AMPR"]=0x0224;
    KC["ASTR"]=0x0225; KC["LPRN"]=0x0226; KC["RPRN"]=0x0227; KC["UNDS"]=0x022D;
    KC["PLUS"]=0x022E; KC["LCBR"]=0x022F; KC["RCBR"]=0x0230; KC["PIPE"]=0x0231;
    KC["DQUO"]=0x0234; KC["TILD"]=0x0235; KC["LT"]=0x0236;   KC["GT"]=0x0237;
    KC["QUES"]=0x0238;

    KC["CW_TOGG"]=0x7C73; KC["QK_LOCK"]=0x7C59;
    KC["AS_DOWN"]=0x7C10; KC["AS_UP"]=0x7C11; KC["AS_RPT"]=0x7C12;
    KC["AS_ON"]=0x7C13;   KC["AS_OFF"]=0x7C14; KC["AS_TOGG"]=0x7C15;

    // Modifiers
    KC["LCTL"]=0xE0; KC["LSFT"]=0xE1; KC["LALT"]=0xE2; KC["LGUI"]=0xE3;
    KC["RCTL"]=0xE4; KC["RSFT"]=0xE5; KC["RALT"]=0xE6; KC["RGUI"]=0xE7;

    // Media / consumer / system (knob press defaults to MUTE = 0xA8)
    KC["PWR"]=0xA5;  KC["SLEP"]=0xA6; KC["WAKE"]=0xA7;
    KC["MUTE"]=0xA8; KC["VOLU"]=0xA9; KC["VOLD"]=0xAA;
    KC["MNXT"]=0xAB; KC["MPRV"]=0xAC; KC["MSTP"]=0xAD; KC["MPLY"]=0xAE; KC["MSEL"]=0xAF;
    KC["EJCT"]=0xB0; KC["MAIL"]=0xB1; KC["CALC"]=0xB2; KC["MYCM"]=0xB3;
    KC["WSCH"]=0xB4; KC["WHOM"]=0xB5; KC["WBAK"]=0xB6; KC["WFWD"]=0xB7;
    KC["WSTP"]=0xB8; KC["WREF"]=0xB9; KC["WFAV"]=0xBA;
    KC["MFFD"]=0xBB; KC["MRWD"]=0xBC; KC["BRIU"]=0xBD; KC["BRID"]=0xBE;
    KC["CPNL"]=0xBF; KC["ASST"]=0xC0; KC["MCTL"]=0xC1; KC["LPAD"]=0xC2;

    // RGB matrix controls (QK_RGB range, 0x7820)
    KC["RGB_TOG"]=0x7820;  KC["RGB_MOD"]=0x7821; KC["RGB_RMOD"]=0x7822;
    KC["RGB_HUI"]=0x7823;  KC["RGB_HUD"]=0x7824; KC["RGB_SAI"]=0x7825;
    KC["RGB_SAD"]=0x7826;  KC["RGB_VAI"]=0x7827; KC["RGB_VAD"]=0x7828;
    KC["RGB_SPI"]=0x7829;  KC["RGB_SPD"]=0x782A;

    // Magic: toggle N-key rollover (Keychron Fn+N)
    KC["NK_TOGG"]=0x7013;

    // Keychron vendor custom keycodes (QK_KB range, 0x7E00) — names match VIA
    const char* kc_custom[] = {
        "LOpt", "ROpt", "LCmd", "RCmd", "MCtrl", "LPad", "Task", "File",
        "SShot", "Cortana", "Siri", "BT1", "BT2", "BT3", "Batt"
    };
    for (int i = 0; i < (int)(sizeof(kc_custom)/sizeof(*kc_custom)); i++)
        KC[kc_custom[i]] = (uint16_t)(0x7E00 + i);

    // Layer-switch keycodes (stride 0x20, layer in low bits). Register the
    // first 8 layers for the picker; nameOf() decodes the full range.
    for (int l = 0; l < 8; l++) {
        std::string n = "(" + std::to_string(l) + ")";
        KC["TO" + n] = (uint16_t)(0x5200 | l);
        KC["MO" + n] = (uint16_t)(0x5220 | l);
        KC["DF" + n] = (uint16_t)(0x5240 | l);
        KC["TG" + n] = (uint16_t)(0x5260 | l);
    }

    // One TDn per tap-dance slot (TD_SLOT_COUNT = 64 in the firmware keymap).
    for (int i = 0; i < 64; i++) KC["TD" + std::to_string(i)] = (uint16_t)(0x5700 | i);

    std::unordered_map<uint16_t, bool> seen;
    for (auto& [n, c] : KC) {
        if (seen[c]) continue;
        seen[c] = true;
        ENTRIES.push_back({n, c});
    }
    std::sort(ENTRIES.begin(), ENTRIES.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
}

std::string nameOf(uint16_t code) {
    buildTables();
    if (code >= 0x5700 && code <= 0x57FF)
        return "TD" + std::to_string(code & 0xFF);
    // Layer-switch keycodes: <prefix>(layer), 0x20 stride
    switch (code & 0xFFE0) {
        case 0x5200: return "TO("  + std::to_string(code & 0x1F) + ")";
        case 0x5220: return "MO("  + std::to_string(code & 0x1F) + ")";
        case 0x5240: return "DF("  + std::to_string(code & 0x1F) + ")";
        case 0x5260: return "TG("  + std::to_string(code & 0x1F) + ")";
        case 0x5280: return "OSL(" + std::to_string(code & 0x1F) + ")";
        case 0x52C0: return "TT("  + std::to_string(code & 0x1F) + ")";
    }
    for (auto& [n, c] : ENTRIES)
        if (c == code) return n;
    char buf[12];
    snprintf(buf, sizeof(buf), "0x%04X", code);
    return buf;
}

uint16_t kcParse(const std::string& raw) {
    buildTables();
    std::string s = raw;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)toupper(c); });
    if (s.rfind("KC_", 0) == 0) s = s.substr(3);
    if (s.rfind("TD", 0) == 0 && s.size() > 2 && std::isdigit((unsigned char)s[2]))
        return (uint16_t)(0x5700 | std::stoi(s.substr(2)));
    if (s.rfind("0X", 0) == 0)
        return (uint16_t)std::stoul(s, nullptr, 16);
    auto it = KC.find(s);
    if (it != KC.end()) return it->second;
    throw std::runtime_error("Unknown keycode: " + raw);
}

const std::vector<std::pair<std::string, uint16_t>>& allKeycodes() {
    buildTables();
    return ENTRIES;
}

// VIA-style category derived from a keycode's value range. First match wins.
static const char* categoryOf(uint16_t c) {
    if (c == 0x00 || c == 0x01 || c == 0x7C73 || c == 0x7C59 ||
        (c >= 0x7C10 && c <= 0x7C15) || c == 0x7013) return "Special";
    if (c <= 0x73 || (c >= 0xE0 && c <= 0xE7) ||
        (c >= 0x0200 && c <= 0x02FF)) return "Basic";
    if (c >= 0xA5 && c <= 0xC2) return "Media";
    if (c >= 0x5200 && c <= 0x52FF) return "Layers";
    if (c >= 0x7820 && c <= 0x782A) return "Lighting";
    if ((c >= 0x5700 && c <= 0x57FF) || (c >= 0x7E00 && c <= 0x7E0E)) return "Custom";
    return "Special";
}

// Natural order: embedded digit runs compare numerically (TD2 < TD10, F2 < F12).
static bool naturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        bool da = isdigit((unsigned char)a[i]), db = isdigit((unsigned char)b[j]);
        if (da && db) {
            size_t ia = i, jb = j;
            while (ia < a.size() && isdigit((unsigned char)a[ia])) ia++;
            while (jb < b.size() && isdigit((unsigned char)b[jb])) jb++;
            long va = std::stol(a.substr(i, ia - i));
            long vb = std::stol(b.substr(j, jb - j));
            if (va != vb) return va < vb;
            i = ia; j = jb;
        } else {
            if (a[i] != b[j]) return a[i] < b[j];
            i++; j++;
        }
    }
    return (a.size() - i) < (b.size() - j);
}

const std::vector<KcCategory>& keycodeCategories() {
    static std::vector<KcCategory> CATS;
    if (!CATS.empty()) return CATS;
    buildTables();
    // Fixed tab order; only non-empty categories are kept.
    const char* order[] = {"Basic", "Media", "Layers", "Lighting", "Special", "Custom"};
    for (const char* name : order) {
        KcCategory cat{name, {}};
        for (auto& e : ENTRIES)
            if (std::string(categoryOf(e.second)) == name) cat.entries.push_back(e);
        std::sort(cat.entries.begin(), cat.entries.end(),
                  [](const auto& a, const auto& b) { return naturalLess(a.first, b.first); });
        if (!cat.entries.empty()) CATS.push_back(std::move(cat));
    }
    return CATS;
}
