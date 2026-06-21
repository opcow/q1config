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
        "  td [N]               show tap-dance slots (default 8, max 64)\n"
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

    if (!strcmp(cmd, "indicators")) {
        if (!connect()) return 1;
        const char* names[] = {"Caps Lock", "Caps Word", "WIN_FN layer"};
        for (int i = 0; i < 3; i++) {
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
