#include "preset.h"
#include "protocol.h"
#include "keycodes.h"
#include <cstdio>

static const char* FLAG_KEYS[] = {
    "caps_word","permissive_hold","hold_on_other_key","retro_tapping","auto_shift"
};

nlohmann::json readConfig(HidDevice& d) {
    auto g = getGlobal(d);
    auto f = getFeat(d);

    nlohmann::json flags;
    for (int bit = 0; bit < 5; bit++)
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

    return {{"schema", 1}, {"tapping_term", g.tt}, {"quick_tap_term", f.quicktap},
            {"autoshift_timeout", f.astimeout}, {"caps_word_timeout", f.cwtimeout},
            {"debounce_time", f.debounce}, {"debounce_method", dbMethodName(f.debounceMethod)},
            {"flags", flags}, {"tap_dance", td}, {"indicators", ind}};
}

int writeConfig(HidDevice& d, const nlohmann::json& p) {
    auto cur = readConfig(d);
    int n = 0;

    if (cur["tapping_term"] != p["tapping_term"]) { setTT(d, p["tapping_term"]); n++; }

    auto param = [&](const char* k, uint8_t pid) {
        if (p.contains(k) && cur[k] != p[k]) { setParam(d, pid, (uint16_t)p[k].get<int>()); n++; }
    };
    param("quick_tap_term", 0); param("autoshift_timeout", 1); param("caps_word_timeout", 2);
    param("debounce_time", 3);

    if (p.contains("debounce_method") && cur["debounce_method"] != p["debounce_method"]) {
        int idx = dbMethodIndex(p["debounce_method"].get<std::string>().c_str());
        if (idx >= 0) { setParam(d, 4, (uint16_t)idx); n++; }
    }

    for (int bit = 0; bit < 5; bit++) {
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

    auto& pi = p["indicators"]; auto& ci = cur["indicators"];
    for (int i = 0; i < 3 && i < (int)pi.size(); i++) {
        if (ci[i]["enabled"] != pi[i]["enabled"] || ci[i]["color"] != pi[i]["color"]) {
            std::string h = pi[i]["color"];
            auto x = [&](int o) { return (uint8_t)std::stoul(h.substr(o, 2), nullptr, 16); };
            setInd(d, i, pi[i]["enabled"], x(1), x(3), x(5)); n++;
        }
    }
    return n;
}
