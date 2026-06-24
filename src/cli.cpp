#include "cli.h"
#include "hid.h"
#include "keycodes.h"
#include "layout.h"
#include "preset.h"
#include "protocol.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

static HidDevice dev;

static bool connect() {
    if (dev.open()) return true;
    fprintf(stderr, "error: Q1 Pro not found (VID 3434 PID 0610)\n");
    return false;
}

static void usage() {
    puts(
        "Usage: q1config <command> [args]\n"
        "\n"
        "  tt [ms]              get or set tapping term\n"
        "  features             show feature flags and timing params\n"
        "  oneshot [ms]         get or set one-shot timeout (0 = never)\n"
        "  td [N]               show tap-dance slots (default 8, max 32)\n"
        "  combos               list combos\n"
        "  combo <i> off|<out> <k1> <k2> [k3] [k4]   set/disable combo i\n"
        "  overrides            list key overrides\n"
        "  override <i> off|<trig> <repl> [mods] [layers]   set/disable override i\n"
        "                       mods: +-joined LCtl,LSft,LAlt,LGui,RCtl,..; layers: e.g. 0,2\n"
        "  keymap [L] [file]    dump keymap (L layers, default 4) to file\n"
        "  indicators           show RGB indicator states\n"
        "  save [file]          save config to JSON preset (default: preset.json)\n"
        "  load <file>          load config from JSON preset\n"
        "  reset                reset config to firmware defaults\n"
        "  reset-keymap         reset full keymap to firmware defaults\n"
    );
}

int cli_main(int argc, char* argv[]) {
    const char* cmd = argv[1];

    if (!strcmp(cmd,"help") || !strcmp(cmd,"--help") || !strcmp(cmd,"-h")) {
        usage(); return 0;
    }

    if (!strcmp(cmd, "tt")) {
        if (!connect()) return 1;
        if (argc < 3) {
            printf("%u ms\n", getGlobal(dev).tt);
        } else {
            int v = atoi(argv[2]);
            if (v < 1 || v > 65535) { fprintf(stderr,"tt: value out of range\n"); return 1; }
            setTT(dev, (uint16_t)v);
        }
        return 0;
    }

    if (!strcmp(cmd, "features")) {
        if (!connect()) return 1;
        auto g = getGlobal(dev); auto f = getFeat(dev);
        printf("tapping_term        %u ms\n", g.tt);
        printf("quick_tap_term      %u ms\n", f.quicktap);
        printf("autoshift_timeout   %u ms\n", f.astimeout);
        printf("caps_word_timeout   %u ms\n", f.cwtimeout);
        printf("debounce            %u ms\n", f.debounce);
        printf("debounce_method     %s\n", dbMethodName(f.debounceMethod));
        printf("oneshot_timeout     %u ms\n", f.oneshotTimeout);
        const char* names[] = {
            "caps_word","permissive_hold","hold_on_other_key","retro_tapping","auto_shift"
        };
        for (int i = 0; i < 5; i++)
            printf("%-22s %s\n", names[i], (f.flags & (1 << i)) ? "on" : "off");
        return 0;
    }

    if (!strcmp(cmd, "td")) {
        if (!connect()) return 1;
        int n = (argc >= 3) ? atoi(argv[2]) : 8;
        if (n < 1 || n > TD_SLOT_COUNT) n = 8;
        printf("%-4s %-10s %-10s %-10s %-8s %s\n",
               "#", "name", "tap", "secondary", "mode", "enabled");
        // Slots are named TD0..TDn so a slot's name is its index (matches the firmware).
        for (int i = 0; i < n; i++) {
            auto s = getTd(dev, i);
            std::string nm = "TD" + std::to_string(i);
            printf("%-4d %-10s %-10s %-10s %-8s %s\n",
                   i, nm.c_str(), nameOf(s.tap).c_str(), nameOf(s.sec).c_str(),
                   s.mode ? "hold" : "double", s.enabled ? "yes" : "no");
        }
        return 0;
    }

    if (!strcmp(cmd, "oneshot")) {
        if (!connect()) return 1;
        if (argc < 3) { printf("%u ms\n", getFeat(dev).oneshotTimeout); }
        else {
            int v = atoi(argv[2]);
            if (v < 0 || v > 65535) { fprintf(stderr,"oneshot: out of range\n"); return 1; }
            setParam(dev, 5, (uint16_t)v);
        }
        return 0;
    }

    if (!strcmp(cmd, "combos")) {
        if (!connect()) return 1;
        printf("%-4s %-10s %-10s %-10s %-10s %-10s %s\n",
               "#", "key1", "key2", "key3", "key4", "output", "enabled");
        for (int i = 0; i < COMBO_SLOT_COUNT; i++) {
            auto c = getCombo(dev, i);
            printf("%-4d %-10s %-10s %-10s %-10s %-10s %s\n", i,
                   nameOf(c.keys[0]).c_str(), nameOf(c.keys[1]).c_str(),
                   nameOf(c.keys[2]).c_str(), nameOf(c.keys[3]).c_str(),
                   nameOf(c.output).c_str(), c.enabled ? "yes" : "no");
        }
        return 0;
    }

    if (!strcmp(cmd, "combo")) {
        if (argc < 4) { fprintf(stderr,"usage: q1config combo <i> off|<out> <k1> <k2> [k3] [k4]\n"); return 1; }
        if (!connect()) return 1;
        int i = atoi(argv[2]);
        if (i < 0 || i >= COMBO_SLOT_COUNT) { fprintf(stderr,"combo: index out of range\n"); return 1; }
        Combo c{};
        if (!strcmp(argv[3], "off")) {
            c = getCombo(dev, i); c.enabled = false;
        } else {
            // argv[3] = output, argv[4..] = input keys (need at least two)
            if (argc < 6) { fprintf(stderr,"combo: need an output and at least two keys\n"); return 1; }
            try {
                c.output = kcParse(argv[3]);
                for (int k = 0; k < COMBO_MAX_KEYS && 4 + k < argc; k++)
                    c.keys[k] = kcParse(argv[4 + k]);
            } catch (const std::exception& e) { fprintf(stderr,"%s\n", e.what()); return 1; }
            c.enabled = true;
        }
        setCombo(dev, i, c);
        return 0;
    }

    if (!strcmp(cmd, "overrides")) {
        if (!connect()) return 1;
        printf("%-4s %-10s %-12s %-18s %-8s %s\n",
               "#", "trigger", "replacement", "trigger-mods", "layers", "enabled");
        for (int i = 0; i < KO_SLOT_COUNT; i++) {
            auto k = getKo(dev, i);
            std::string mods; const char* const* mn = modMaskNames();
            for (int b = 0; b < 8; b++) if (k.triggerMods & (1<<b)) { if(!mods.empty()) mods+="+"; mods+=mn[b]; }
            if (mods.empty()) mods = "-";
            std::string ly; for (int l = 0; l < 8; l++) if (k.layers & (1<<l)) { if(!ly.empty()) ly+=","; ly+=std::to_string(l); }
            if (ly.empty()) ly = "-";
            printf("%-4d %-10s %-12s %-18s %-8s %s\n", i,
                   nameOf(k.trigger).c_str(), nameOf(k.replacement).c_str(),
                   mods.c_str(), ly.c_str(), k.enabled ? "yes" : "no");
        }
        return 0;
    }

    if (!strcmp(cmd, "override")) {
        if (argc < 4) { fprintf(stderr,"usage: q1config override <i> off|<trig> <repl> [mods] [layers]\n"); return 1; }
        if (!connect()) return 1;
        int i = atoi(argv[2]);
        if (i < 0 || i >= KO_SLOT_COUNT) { fprintf(stderr,"override: index out of range\n"); return 1; }
        KeyOverride k{};
        if (!strcmp(argv[3], "off")) {
            k = getKo(dev, i); k.enabled = false;
        } else {
            if (argc < 5) { fprintf(stderr,"override: need trigger and replacement\n"); return 1; }
            try { k.trigger = kcParse(argv[3]); k.replacement = kcParse(argv[4]); }
            catch (const std::exception& e) { fprintf(stderr,"%s\n", e.what()); return 1; }
            // optional mods (+-joined names) and layers (comma list)
            const char* const* mn = modMaskNames();
            if (argc >= 6) {
                std::string s = argv[5], tok; size_t p = 0;
                while (p <= s.size()) {
                    size_t q = s.find('+', p);
                    tok = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
                    for (int b = 0; b < 8; b++) if (tok == mn[b]) k.triggerMods |= (uint8_t)(1<<b);
                    if (q == std::string::npos) break; p = q + 1;
                }
            }
            if (argc >= 7) {
                std::string s = argv[6]; size_t p = 0;
                while (p <= s.size()) {
                    size_t q = s.find(',', p);
                    int l = atoi(s.substr(p, q == std::string::npos ? std::string::npos : q - p).c_str());
                    if (l >= 0 && l < 8) k.layers |= (uint8_t)(1 << l);
                    if (q == std::string::npos) break; p = q + 1;
                }
            } else {
                k.layers = 0x0F;  // all four layers by default
            }
            k.options = 0x07;     // ko_options_all_activations
            k.enabled = true;
        }
        setKo(dev, i, k);
        return 0;
    }

    if (!strcmp(cmd, "indicators")) {
        if (!connect()) return 1;
        const char* names[] = {"Caps Lock", "Caps Word", "Win FN layer", "Num Lock",
                              "Scroll Lock", "Mac FN layer", "Windows mode", "One-shot mod"};
        for (int i = 0; i < INDICATOR_COUNT; i++) {
            auto v = getInd(dev, i);
            printf("%-14s  %s  #%02X%02X%02X\n",
                   names[i], v.enabled ? "on " : "off", v.r, v.g, v.b);
        }
        return 0;
    }

    if (!strcmp(cmd, "save")) {
        if (!connect()) return 1;
        const char* path = (argc > 2) ? argv[2] : "preset.json";
        auto cfg = readConfig(dev);
        std::ofstream f(path);
        if (!f) { fprintf(stderr,"cannot write %s\n", path); return 1; }
        f << cfg.dump(2) << "\n";
        printf("Saved to %s\n", path);
        return 0;
    }

    if (!strcmp(cmd, "load")) {
        if (argc < 3) { fprintf(stderr,"usage: q1config load <file>\n"); return 1; }
        if (!connect()) return 1;
        std::ifstream f(argv[2]);
        if (!f) { fprintf(stderr,"cannot open %s\n", argv[2]); return 1; }
        nlohmann::json p; f >> p;
        int n = writeConfig(dev, p);
        printf("Loaded %s (%d change(s))\n", argv[2], n);
        return 0;
    }

    if (!strcmp(cmd, "keymap")) {
        if (!connect()) return 1;
        int layers = (argc >= 3) ? atoi(argv[2]) : 4;
        const char* path = (argc >= 4) ? argv[3] : "keymap_dump.txt";
        FILE* out = nullptr;
#ifdef _WIN32
        fopen_s(&out, path, "w");
#else
        out = fopen(path, "w");
#endif
        if (!out) { fprintf(stderr, "cannot write %s\n", path); return 1; }
        for (int l = 0; l < layers; l++) {
            fprintf(out, "== layer %d ==\n", l);
            for (auto& k : layout()) {
                uint16_t kc = viaGet(dev, l, k.row, k.col);
                std::string nm = nameOf(kc);
                bool unnamed = (nm.size() > 1 && nm[0] == '0' && nm[1] == 'x');
                fprintf(out, "  [%d,%2d] 0x%04X  %s%s\n", k.row, k.col, kc,
                        nm.c_str(), unnamed ? "   <-- UNNAMED" : "");
            }
        }
        fclose(out);
        return 0;
    }

    if (!strcmp(cmd, "reset")) {
        if (!connect()) return 1;
        resetCfg(dev);
        puts("Config reset to firmware defaults.");
        return 0;
    }

    if (!strcmp(cmd, "reset-keymap")) {
        if (!connect()) return 1;
        viaKmReset(dev);
        puts("Keymap reset to firmware defaults.");
        return 0;
    }

    fprintf(stderr, "Unknown command: %s  (try 'q1config help')\n", cmd);
    return 1;
}
