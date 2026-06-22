#include "preset.h"
#include "protocol.h"
#include "keycodes.h"
#include <cstdio>

static const char* FLAG_KEYS[] = {
    "caps_word","permissive_hold","hold_on_other_key","retro_tapping","auto_shift",
    "caps_word_double_shift","caps_word_both_shifts"
};
static constexpr int FLAG_COUNT = (int)(sizeof(FLAG_KEYS) / sizeof(FLAG_KEYS[0]));

// 8-bit modifier mask <-> JSON array of mod names (LCtl..RGui).
static nlohmann::json maskToNames(uint8_t mask) {
    auto names = modMaskNames();
    nlohmann::json a = nlohmann::json::array();
    for (int b = 0; b < 8; b++) if (mask & (1 << b)) a.push_back(names[b]);
    return a;
}
static uint8_t namesToMask(const nlohmann::json& a) {
    auto names = modMaskNames();
    uint8_t m = 0;
    for (auto& v : a) { std::string s = v; for (int b = 0; b < 8; b++) if (s == names[b]) m |= (uint8_t)(1 << b); }
    return m;
}
static nlohmann::json layersToArr(uint8_t mask) {
    nlohmann::json a = nlohmann::json::array();
    for (int l = 0; l < 8; l++) if (mask & (1 << l)) a.push_back(l);
    return a;
}
static uint8_t arrToLayers(const nlohmann::json& a) {
    uint8_t m = 0;
    for (auto& v : a) { int l = v.get<int>(); if (l >= 0 && l < 8) m |= (uint8_t)(1 << l); }
    return m;
}

nlohmann::json readConfig(HidDevice& d) {
    auto g = getGlobal(d);
    auto f = getFeat(d);

    nlohmann::json flags;
    for (int bit = 0; bit < FLAG_COUNT; bit++)
        flags[FLAG_KEYS[bit]] = !!(f.flags & (1 << bit));

    nlohmann::json td = nlohmann::json::array();
    for (int i = 0; i < TD_SLOT_COUNT; i++) {
        auto s = getTd(d, i);
        td.push_back({{"tap", nameOf(s.tap)}, {"secondary", nameOf(s.sec)},
                      {"mode", s.mode ? "hold" : "double"}, {"enabled", s.enabled}});
    }

    nlohmann::json ind = nlohmann::json::array();
    for (int i = 0; i < 3; i++) {
        auto v = getInd(d, i);
        char hex[8];
        snprintf(hex, sizeof(hex), "#%02X%02X%02X", v.r, v.g, v.b);
        ind.push_back({{"enabled", v.enabled}, {"color", hex}});
    }

    nlohmann::json combos = nlohmann::json::array();
    for (int i = 0; i < COMBO_SLOT_COUNT; i++) {
        auto c = getCombo(d, i);
        nlohmann::json keys = nlohmann::json::array();
        for (int k = 0; k < COMBO_MAX_KEYS; k++) keys.push_back(nameOf(c.keys[k]));
        combos.push_back({{"keys", keys}, {"output", nameOf(c.output)}, {"enabled", c.enabled}});
    }

    nlohmann::json kos = nlohmann::json::array();
    for (int i = 0; i < KO_SLOT_COUNT; i++) {
        auto k = getKo(d, i);
        kos.push_back({{"trigger", nameOf(k.trigger)}, {"replacement", nameOf(k.replacement)},
                       {"trigger_mods", maskToNames(k.triggerMods)},
                       {"suppressed_mods", maskToNames(k.suppressedMods)},
                       {"negative_mods", maskToNames(k.negativeMods)},
                       {"layers", layersToArr(k.layers)}, {"options", k.options},
                       {"enabled", k.enabled}});
    }

    // Full dynamic keymap (all layers, row-major), keycode names. Lets a preset
    // reproduce key->TD (and any other) assignments now that the firmware ships
    // a stock keymap.
    nlohmann::json keymap = nlohmann::json::array();
    for (int l = 0; l < KM_LAYERS; l++) {
        nlohmann::json layer = nlohmann::json::array();
        for (int r = 0; r < KM_ROWS; r++)
            for (int c = 0; c < KM_COLS; c++)
                layer.push_back(nameOf(viaGet(d, l, r, c)));
        keymap.push_back(layer);
    }

    return {{"schema", 1}, {"tapping_term", g.tt}, {"quick_tap_term", f.quicktap},
            {"autoshift_timeout", f.astimeout}, {"caps_word_timeout", f.cwtimeout},
            {"debounce_time", f.debounce}, {"debounce_method", dbMethodName(f.debounceMethod)},
            {"oneshot_timeout", f.oneshotTimeout},
            {"flags", flags}, {"tap_dance", td}, {"combos", combos},
            {"key_overrides", kos}, {"indicators", ind}, {"keymap", keymap}};
}

int writeConfig(HidDevice& d, const nlohmann::json& p) {
    auto cur = readConfig(d);
    int n = 0;

    if (cur["tapping_term"] != p["tapping_term"]) { setTT(d, p["tapping_term"]); n++; }

    auto param = [&](const char* k, uint8_t pid) {
        if (p.contains(k) && cur[k] != p[k]) { setParam(d, pid, (uint16_t)p[k].get<int>()); n++; }
    };
    param("quick_tap_term", 0); param("autoshift_timeout", 1); param("caps_word_timeout", 2);
    param("debounce_time", 3); param("oneshot_timeout", 5);

    if (p.contains("debounce_method") && cur["debounce_method"] != p["debounce_method"]) {
        int idx = dbMethodIndex(p["debounce_method"].get<std::string>().c_str());
        if (idx >= 0) { setParam(d, 4, (uint16_t)idx); n++; }
    }

    for (int bit = 0; bit < FLAG_COUNT; bit++) {
        const char* k = FLAG_KEYS[bit];
        bool want = p["flags"].value(k, false);
        if ((bool)cur["flags"][k] != want) { setFlag(d, bit, want); n++; }
    }

    auto& ptd = p["tap_dance"]; auto& ctd = cur["tap_dance"];
    for (int i = 0; i < TD_SLOT_COUNT && i < (int)ptd.size(); i++) {
        if (ctd[i]["tap"] != ptd[i]["tap"] || ctd[i]["secondary"] != ptd[i]["secondary"]) {
            setTdKc(d, i, kcParse(ptd[i]["tap"]), kcParse(ptd[i]["secondary"])); n++;
        }
        if (ctd[i]["mode"] != ptd[i]["mode"])
            { setTdMode(d, i, ptd[i]["mode"] == "hold" ? 1 : 0); n++; }
        if (ctd[i]["enabled"] != ptd[i]["enabled"])
            { setTdEn(d, i, ptd[i]["enabled"]); n++; }
    }

    if (p.contains("combos")) {
        auto& pc = p["combos"]; auto& cc = cur["combos"];
        for (int i = 0; i < COMBO_SLOT_COUNT && i < (int)pc.size(); i++) {
            if (cc[i] == pc[i]) continue;
            Combo c{};
            for (int k = 0; k < COMBO_MAX_KEYS; k++)
                c.keys[k] = (k < (int)pc[i]["keys"].size()) ? kcParse(pc[i]["keys"][k]) : 0;
            c.output  = kcParse(pc[i]["output"]);
            c.enabled = pc[i].value("enabled", false);
            setCombo(d, i, c); n++;
        }
    }

    if (p.contains("key_overrides")) {
        auto& pk = p["key_overrides"]; auto& ck = cur["key_overrides"];
        for (int i = 0; i < KO_SLOT_COUNT && i < (int)pk.size(); i++) {
            if (ck[i] == pk[i]) continue;
            KeyOverride k{};
            k.trigger        = kcParse(pk[i]["trigger"]);
            k.replacement    = kcParse(pk[i]["replacement"]);
            k.triggerMods    = namesToMask(pk[i].value("trigger_mods", nlohmann::json::array()));
            k.suppressedMods = namesToMask(pk[i].value("suppressed_mods", nlohmann::json::array()));
            k.negativeMods   = namesToMask(pk[i].value("negative_mods", nlohmann::json::array()));
            k.layers         = arrToLayers(pk[i].value("layers", nlohmann::json::array()));
            k.options        = (uint8_t)pk[i].value("options", 7);
            k.enabled        = pk[i].value("enabled", false);
            setKo(d, i, k); n++;
        }
    }

    auto& pi = p["indicators"]; auto& ci = cur["indicators"];
    for (int i = 0; i < 3 && i < (int)pi.size(); i++) {
        if (ci[i]["enabled"] != pi[i]["enabled"] || ci[i]["color"] != pi[i]["color"]) {
            std::string h = pi[i]["color"];
            auto x = [&](int o) { return (uint8_t)std::stoul(h.substr(o, 2), nullptr, 16); };
            setInd(d, i, pi[i]["enabled"], x(1), x(3), x(5)); n++;
        }
    }

    if (p.contains("keymap")) {
        auto& pk = p["keymap"]; auto& ck = cur["keymap"];
        for (int l = 0; l < KM_LAYERS && l < (int)pk.size(); l++)
            for (int i = 0; i < KM_ROWS * KM_COLS && i < (int)pk[l].size(); i++)
                if (ck[l][i] != pk[l][i]) {
                    viaSet(d, l, i / KM_COLS, i % KM_COLS, kcParse(pk[l][i])); n++;
                }
    }
    return n;
}
