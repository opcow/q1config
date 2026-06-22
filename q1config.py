#!/usr/bin/env python3
"""Configure Q1 Pro tap dance / tapping term over raw HID.

Requires: pip install hidapi

Commands:
    get                          show global config (tapping term, enabled, mode)
    tt <ms>                      set global tapping term
    list                         show all tap dance slots
    show <idx>                   show one tap dance slot
    en <idx> <0|1>              enable/disable a slot's secondary action
    mode <idx> <double|hold>     set a slot's secondary trigger
    kc <idx> <tap> <secondary>   set a slot's keycodes (names, e.g. SCLN COLN)
    reset                        restore defaults

  Assign physical keys to slots (runtime remap, persists in EEPROM):
    assign <layer> <row> <col> <slot>   make a key trigger tap-dance <slot>
    unassign <layer> <row> <col> <kc>   restore a key to a normal keycode
    keyget <layer> <row> <col>          show the keycode at a position
    keyset <layer> <row> <col> <kc>     set any keycode at a position
    id                                  press a key; prints its row/col
    dump <layer>                        print a layer's keycode grid
    kmreset                             reset WHOLE keymap to compiled defaults

  Feature config:
    features                            show feature flags + timeouts
    capsword <0|1>                      enable/disable Caps Word
    cwtimeout <ms>                      Caps Word idle timeout (0 = never)
    dtapshift <0|1>                     double-tap Left Shift turns on Caps Word
    bothshift <0|1>                     holding both shifts turns on Caps Word
    permissive <0|1>                    permissive hold
    hokp <0|1>                          hold on other key press
    retro <0|1>                         retro tapping
    quicktap <ms>                       quick tap term
    autoshift <0|1>                     Auto Shift on/off
    astimeout <ms>                      Auto Shift timeout
    debounce <ms>                       matrix debounce time (0-255, 0 = none)
    dbmethod <name|idx>                 debounce algorithm: none, sym_defer_g,
                                          sym_eager_pk, asym_eager_defer_pk
    oneshot [ms]                        one-shot timeout (0 = never expire)

  Combos (16 slots) and key overrides (16 slots):
    combos                              list combos
    combo <i> off                       disable a combo
    combo <i> <out> <k1> <k2> [k3] [k4] set a combo (>=2 input keys)
    overrides                           list key overrides
    override <i> off                    disable an override
    override <i> <trig> <repl> [mods] [layers]   set an override
                                          mods = +-joined LCtl,LSft,..; layers = e.g. 0,2

  Presets (JSON files in ./presets/, shared with the WebHID GUI):
    presets                             list available presets
    save <name>                         snapshot current config -> presets/<name>.json
    load <name>                         apply a preset (writes only what differs)
    mine                                alias for `load mine`

  RGB state indicators (capslock / capsword / winfn):
    indicators                          show indicator on/off + colors
    indicator <name> <on|off> [color]   set; color = "R G B" or "#rrggbb"

    mine                                apply the author's personal setup

Keycodes may be names (A, F12, SCLN, COLN, HOME, ENT, CW_TOGG, LOCK, AS_TOGG...),
shifted form S(SCLN), a tap-dance slot TD3 / TD(3), mod-tap MT(LSft,A),
layer-tap LT(1,SPC), one-shot OSM(LSft) / OSL(1), modded MOD(LCtl,A), or raw
numbers (0x0233 / 563).
Layers: 0 MAC_BASE  1 MAC_FN  2 WIN_BASE  3 WIN_FN. Matrix is 6 rows x 16 cols.
Tap dance slots are named TD0..TD31 (the name is the slot index). Slots 0-7 ship
with default keycodes (TD0 caps, TD1 home/end, TD2 esc, TD3 ;/:, TD4-7 F9-F12);
8-31 start blank.
"""
import sys
import json
from pathlib import Path
import hid

TD_SLOT_COUNT = 32
COMBO_SLOT_COUNT = 16
COMBO_MAX_KEYS = 4
KO_SLOT_COUNT = 16
FLAG_KEYS = ["caps_word", "permissive_hold", "hold_on_other_key", "retro_tapping", "auto_shift",
             "caps_word_double_shift", "caps_word_both_shifts"]
# 8-bit modifier-mask bit names for key-override mods (QMK MOD_* order)
MOD_MASK_NAMES = ["LCtl", "LSft", "LAlt", "LGui", "RCtl", "RSft", "RAlt", "RGui"]
PRESET_DIR = Path(__file__).resolve().parent / "presets"

VID, PID = 0x3434, 0x0610
USAGE_PAGE, USAGE = 0xFF60, 0x61
CMD = 0xAC
GET_GLOBAL, SET_TT, SET_TD_EN, SET_TD_MODE, RESET, GET_TD, SET_TD_KC, IDENTIFY = \
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
GET_FEATURES, SET_FLAG, SET_PARAM = 0x09, 0x0A, 0x0B
GET_INDICATOR, SET_INDICATOR = 0x0C, 0x0D
GET_COMBO, SET_COMBO, GET_KO, SET_KO = 0x0E, 0x0F, 0x10, 0x11
# feature_flags bit positions (must match keymap.c FF_* )
FF_CAPS_WORD, FF_PERMISSIVE, FF_HOKP, FF_RETRO, FF_AUTOSHIFT = 0, 1, 2, 3, 4
FF_CW_DOUBLE_SHIFT, FF_CW_BOTH_SHIFTS = 5, 6
# SET_PARAM ids
PARAM_QUICKTAP, PARAM_ASTIMEOUT, PARAM_CWTIMEOUT, PARAM_DEBOUNCE, PARAM_DEBOUNCE_METHOD, PARAM_ONESHOT = 0, 1, 2, 3, 4, 5
# debounce method index -> canonical name (must match keymap.c dispatcher order)
DEBOUNCE_METHODS = ["none", "sym_defer_g", "sym_eager_pk", "asym_eager_defer_pk"]
# indicator indices (must match keymap.c IND_* )
IND_NAMES = ["capslock", "capsword", "winfn"]
# Standard VIA dynamic-keymap commands (handled by via.c, big-endian keycode)
VIA_GET_KEYCODE, VIA_SET_KEYCODE, VIA_KEYMAP_RESET = 0x04, 0x05, 0x06
QK_TAP_DANCE = 0x5700
ROWS, COLS, LAYERS = 6, 16, 4

# Slots are named TD0..TD63 so a slot's name is its index (matches the firmware).
TD_NAMES = [f"TD{i}" for i in range(TD_SLOT_COUNT)]

# --- keycode name -> value table (QMK basic keycodes / HID usage IDs) ---
NAMES = {"NO": 0x00, "TRNS": 0x01}
for i, c in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ"):
    NAMES[c] = 0x04 + i
for i, c in enumerate("123456789"):
    NAMES[c] = 0x1E + i
NAMES["0"] = 0x27
NAMES.update({
    "ENT": 0x28, "ESC": 0x29, "BSPC": 0x2A, "TAB": 0x2B, "SPC": 0x2C,
    "MINS": 0x2D, "EQL": 0x2E, "LBRC": 0x2F, "RBRC": 0x30, "BSLS": 0x31,
    "SCLN": 0x33, "QUOT": 0x34, "GRV": 0x35, "COMM": 0x36, "DOT": 0x37,
    "SLSH": 0x38, "CAPS": 0x39,
    "PSCR": 0x46, "SCRL": 0x47, "PAUS": 0x48, "INS": 0x49, "HOME": 0x4A,
    "PGUP": 0x4B, "DEL": 0x4C, "END": 0x4D, "PGDN": 0x4E,
    "RGHT": 0x4F, "LEFT": 0x50, "DOWN": 0x51, "UP": 0x52, "NUM": 0x53,
})
for i in range(1, 13):       # F1..F12
    NAMES[f"F{i}"] = 0x3A + (i - 1)
for i in range(13, 25):      # F13..F24
    NAMES[f"F{i}"] = 0x68 + (i - 13)
# common shifted symbols (S(x) = LSFT | x, modifier bit 0x0200)
NAMES.update({
    "COLN": 0x0233, "EXLM": 0x021E, "AT": 0x021F, "HASH": 0x0220,
    "DLR": 0x0221, "PERC": 0x0222, "CIRC": 0x0223, "AMPR": 0x0224,
    "ASTR": 0x0225, "LPRN": 0x0226, "RPRN": 0x0227, "UNDS": 0x022D,
    "PLUS": 0x022E, "LCBR": 0x022F, "RCBR": 0x0230, "PIPE": 0x0231,
    "DQUO": 0x0234, "TILD": 0x0235, "LT": 0x0236, "GT": 0x0237, "QUES": 0x0238,
})
# QMK feature keycodes (assignable to keys via assign/keyset)
NAMES.update({
    "CW_TOGG": 0x7C73,                       # Caps Word toggle
    "QK_LOCK": 0x7C59, "LOCK": 0x7C59,       # Key Lock
    "AS_DOWN": 0x7C10, "AS_UP": 0x7C11, "AS_RPT": 0x7C12,
    "AS_ON": 0x7C13, "AS_OFF": 0x7C14, "AS_TOGG": 0x7C15,
})
CODE_TO_NAME = {v: k for k, v in NAMES.items()}

# QMK keycode-range bases for MT/LT/OSM/modded builder forms.
QK_MODS, QK_MOD_TAP, QK_LAYER_TAP, QK_OSM = 0x0100, 0x2000, 0x4000, 0x52A0
QK_OSL = 0x5280


def mods5_to_str(m):
    """5-bit packed mod field (bits 0-3 CSAG, bit4 right) -> '+'-joined tokens."""
    side = ("RCtl", "RSft", "RAlt", "RGui") if m & 0x10 else ("LCtl", "LSft", "LAlt", "LGui")
    out = [side[i] for i in range(4) if m & (1 << i)]
    return "+".join(out) if out else "0"


def mods5_from_str(s):
    if s in ("", "0"):
        return 0
    m = 0
    for tok in s.split("+"):
        t = tok.strip().upper()
        if not t:
            continue
        right = t[0] == "R"
        bit = {"CTL": 0, "SFT": 1, "ALT": 2, "GUI": 3}.get(t[1:])
        if bit is None:
            sys.exit(f"Unknown modifier: {tok}")
        m |= (1 << bit) | (0x10 if right else 0)
    return m


def kc(token):
    t = token.upper()
    if t.startswith("KC_"):
        t = t[3:]
    if t.startswith("S(") and t.endswith(")"):
        return 0x0200 | kc(t[2:-1])
    if t.startswith("MT(") and t.endswith(")"):
        mods, base = t[3:-1].split(",", 1)
        return QK_MOD_TAP | (mods5_from_str(mods) << 8) | (kc(base) & 0xFF)
    if t.startswith("LT(") and t.endswith(")"):
        layer, base = t[3:-1].split(",", 1)
        return QK_LAYER_TAP | ((int(layer) & 0xF) << 8) | (kc(base) & 0xFF)
    if t.startswith("OSM(") and t.endswith(")"):
        return QK_OSM | (mods5_from_str(t[4:-1]) & 0x1F)
    if t.startswith("OSL(") and t.endswith(")"):
        return QK_OSL | (int(t[4:-1]) & 0x1F)
    if t.startswith("MOD(") and t.endswith(")"):
        mods, base = t[4:-1].split(",", 1)
        return QK_MODS | (mods5_from_str(mods) << 8) | (kc(base) & 0xFF)
    if t.startswith("TD(") and t.endswith(")"):
        return QK_TAP_DANCE | int(t[3:-1])
    if t.startswith("TD") and t[2:].isdigit():
        return QK_TAP_DANCE | int(t[2:])
    if t.startswith("0X"):
        return int(t, 16)
    if t in NAMES:
        return NAMES[t]
    if token.isdigit():
        return int(token)
    sys.exit(f"Unknown keycode: {token}")


def name_of(code):
    if QK_TAP_DANCE <= code <= QK_TAP_DANCE | 0xFF:
        return f"TD{code & 0xFF}"
    if 0x5280 <= code <= 0x529F:
        return f"OSL({code & 0x1F})"
    if 0x52A0 <= code <= 0x52BF:
        return f"OSM({mods5_to_str(code & 0x1F)})"
    if code in CODE_TO_NAME:
        return CODE_TO_NAME[code]
    if QK_MOD_TAP <= code <= 0x3FFF:
        return f"MT({mods5_to_str((code >> 8) & 0x1F)},{name_of(code & 0xFF)})"
    if QK_LAYER_TAP <= code <= 0x4FFF:
        return f"LT({(code >> 8) & 0xF},{name_of(code & 0xFF)})"
    if QK_MODS <= code <= 0x1FFF:
        return f"MOD({mods5_to_str((code >> 8) & 0x1F)},{name_of(code & 0xFF)})"
    return f"0x{code:04X}"


def modmask_to_list(mask):
    return [MOD_MASK_NAMES[b] for b in range(8) if mask & (1 << b)]


def modmask_from_list(names):
    m = 0
    for nm in names:
        if nm in MOD_MASK_NAMES:
            m |= 1 << MOD_MASK_NAMES.index(nm)
    return m


def open_dev():
    for d in hid.enumerate(VID, PID):
        if d["usage_page"] == USAGE_PAGE and d["usage"] == USAGE:
            h = hid.device()
            h.open_path(d["path"])
            return h
    sys.exit("Keyboard raw HID interface not found (plugged in via USB?)")


def send_raw(h, payload):
    buf = bytes(payload) + bytes(32 - len(payload))
    h.write(b"\x00" + buf)  # leading 0 = report ID
    r = h.read(32, timeout_ms=1000)
    if not r:
        sys.exit("No response from keyboard")
    return r


def send(h, payload):
    # 0xAC commands: byte[1] of the reply is a status code (0 = OK).
    r = send_raw(h, payload)
    if r[1] != 0x00:
        sys.exit("Keyboard returned ERROR (bad index or value?)")
    return r


def via_get_keycode(h, layer, row, col):
    r = send_raw(h, [VIA_GET_KEYCODE, layer, row, col])
    return (r[4] << 8) | r[5]


def via_set_keycode(h, layer, row, col, code):
    if not (0 <= layer < LAYERS and 0 <= row < ROWS and 0 <= col < COLS):
        sys.exit(f"position out of range (layer<{LAYERS} row<{ROWS} col<{COLS})")
    send_raw(h, [VIA_SET_KEYCODE, layer, row, col, (code >> 8) & 0xFF, code & 0xFF])


def show_slot(h, i):
    r = send(h, [CMD, GET_TD, i])
    tap = r[3] | (r[4] << 8)
    sec = r[5] | (r[6] << 8)
    en = "on " if r[7] else "off"
    mode = "hold  " if r[8] else "double"
    print(f"  [{i}] {TD_NAMES[i]:<9} tap={name_of(tap):<6} "
          f"secondary={name_of(sec):<6} mode={mode} enabled={en}")


def set_flag(h, bit, val):
    send(h, [CMD, SET_FLAG, bit, 1 if int(val) else 0])


def set_param(h, pid, val):
    val = int(val)
    send(h, [CMD, SET_PARAM, pid, val & 0xFF, (val >> 8) & 0xFF])


def show_features(h):
    r = send(h, [CMD, GET_FEATURES])
    flags = r[2] | (r[3] << 8)
    quicktap = r[4] | (r[5] << 8)
    astimeout = r[6] | (r[7] << 8)
    cwtimeout = r[8] | (r[9] << 8)
    debounce = r[10]
    dbmethod = r[11]
    oneshot = r[12] | (r[13] << 8)
    def on(b):
        return "on" if flags & (1 << b) else "off"
    print(f"caps_word={on(FF_CAPS_WORD)}  cw_timeout={cwtimeout}  "
          f"double_shift={on(FF_CW_DOUBLE_SHIFT)}  both_shifts={on(FF_CW_BOTH_SHIFTS)}")
    print(f"permissive_hold={on(FF_PERMISSIVE)}  hold_on_other_key={on(FF_HOKP)}  "
          f"retro={on(FF_RETRO)}  quick_tap={quicktap}")
    print(f"auto_shift={on(FF_AUTOSHIFT)}  as_timeout={astimeout}")
    print(f"debounce={debounce}ms  method={dbmethod_name(dbmethod)}")
    print(f"oneshot_timeout={oneshot}")


def dbmethod_name(i):
    return DEBOUNCE_METHODS[i] if 0 <= i < len(DEBOUNCE_METHODS) else f"?{i}"


def dbmethod_index(token):
    if token.isdigit():
        return int(token)
    t = token.lower()
    if t in DEBOUNCE_METHODS:
        return DEBOUNCE_METHODS.index(t)
    sys.exit(f"Unknown debounce method: {token} (use {', '.join(DEBOUNCE_METHODS)})")


def ind_index(name):
    if name.isdigit():
        return int(name)
    if name.lower() in IND_NAMES:
        return IND_NAMES.index(name.lower())
    sys.exit(f"Unknown indicator: {name} (use {', '.join(IND_NAMES)})")


def parse_color(tokens):
    """Accept 'R G B' (three ints) or a single '#rrggbb'. Returns (r, g, b)."""
    if len(tokens) == 1 and tokens[0].startswith("#"):
        v = int(tokens[0][1:], 16)
        return (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF
    if len(tokens) == 3:
        return tuple(int(t) & 0xFF for t in tokens)
    sys.exit("color must be 'R G B' (0-255 each) or '#rrggbb'")


def show_indicator(h, i):
    r = send(h, [CMD, GET_INDICATOR, i])
    en = "on " if r[3] else "off"
    print(f"  [{i}] {IND_NAMES[i]:<9} {en}  color=({r[4]},{r[5]},{r[6]})  #{r[4]:02X}{r[5]:02X}{r[6]:02X}")


def set_indicator(h, i, enabled, rgb):
    send(h, [CMD, SET_INDICATOR, i, 1 if enabled else 0, rgb[0], rgb[1], rgb[2]])


# ----- Combos & key overrides -----
def get_combo(h, i):
    r = send(h, [CMD, GET_COMBO, i])
    keys = [r[3 + k * 2] | (r[4 + k * 2] << 8) for k in range(COMBO_MAX_KEYS)]
    return {"keys": keys, "output": r[11] | (r[12] << 8), "enabled": bool(r[13])}


def set_combo(h, i, keys, output, enabled):
    p = [CMD, SET_COMBO, i]
    for k in range(COMBO_MAX_KEYS):
        v = keys[k] if k < len(keys) else 0
        p += [v & 0xFF, (v >> 8) & 0xFF]
    p += [output & 0xFF, (output >> 8) & 0xFF, 1 if enabled else 0]
    send(h, p)


def get_ko(h, i):
    r = send(h, [CMD, GET_KO, i])
    return {
        "trigger": r[3] | (r[4] << 8), "replacement": r[5] | (r[6] << 8),
        "trigger_mods": r[7], "suppressed_mods": r[8], "negative_mods": r[9],
        "layers": r[10], "options": r[11], "enabled": bool(r[12]),
    }


def set_ko(h, i, k):
    send(h, [CMD, SET_KO, i,
             k["trigger"] & 0xFF, (k["trigger"] >> 8) & 0xFF,
             k["replacement"] & 0xFF, (k["replacement"] >> 8) & 0xFF,
             k["trigger_mods"] & 0xFF, k["suppressed_mods"] & 0xFF,
             k["negative_mods"] & 0xFF, k["layers"] & 0xFF, k["options"] & 0xFF,
             1 if k["enabled"] else 0])


def show_combo(h, i):
    c = get_combo(h, i)
    keys = " ".join(name_of(x) for x in c["keys"])
    print(f"  [{i:>2}] {keys:<28} -> {name_of(c['output']):<8} {'on' if c['enabled'] else 'off'}")


def show_override(h, i):
    k = get_ko(h, i)
    mods = "+".join(modmask_to_list(k["trigger_mods"])) or "-"
    layers = ",".join(str(l) for l in range(8) if k["layers"] & (1 << l)) or "-"
    print(f"  [{i:>2}] {name_of(k['trigger']):<8} -> {name_of(k['replacement']):<8} "
          f"mods={mods:<14} layers={layers:<6} {'on' if k['enabled'] else 'off'}")


# ----- Presets (filesystem, shared JSON schema with the WebHID GUI) -----
def read_config(h):
    """Snapshot the device's full 0xAC config into a preset dict (keycode names)."""
    g = send(h, [CMD, GET_GLOBAL])
    f = send(h, [CMD, GET_FEATURES])
    flags = f[2] | (f[3] << 8)
    preset = {
        "schema": 1,
        "tapping_term": g[2] | (g[3] << 8),
        "quick_tap_term": f[4] | (f[5] << 8),
        "autoshift_timeout": f[6] | (f[7] << 8),
        "caps_word_timeout": f[8] | (f[9] << 8),
        "debounce_time": f[10],
        "debounce_method": dbmethod_name(f[11]),
        "oneshot_timeout": f[12] | (f[13] << 8),
        "flags": {key: bool(flags & (1 << bit)) for bit, key in enumerate(FLAG_KEYS)},
        "tap_dance": [],
        "combos": [],
        "key_overrides": [],
        "indicators": [],
    }
    for i in range(TD_SLOT_COUNT):
        r = send(h, [CMD, GET_TD, i])
        preset["tap_dance"].append({
            "tap": name_of(r[3] | (r[4] << 8)),
            "secondary": name_of(r[5] | (r[6] << 8)),
            "mode": "hold" if r[8] else "double",
            "enabled": bool(r[7]),
        })
    for i in range(COMBO_SLOT_COUNT):
        c = get_combo(h, i)
        preset["combos"].append({
            "keys": [name_of(x) for x in c["keys"]],
            "output": name_of(c["output"]),
            "enabled": c["enabled"],
        })
    for i in range(KO_SLOT_COUNT):
        k = get_ko(h, i)
        preset["key_overrides"].append({
            "trigger": name_of(k["trigger"]),
            "replacement": name_of(k["replacement"]),
            "trigger_mods": modmask_to_list(k["trigger_mods"]),
            "suppressed_mods": modmask_to_list(k["suppressed_mods"]),
            "negative_mods": modmask_to_list(k["negative_mods"]),
            "layers": [l for l in range(8) if k["layers"] & (1 << l)],
            "options": k["options"],
            "enabled": k["enabled"],
        })
    for i in range(len(IND_NAMES)):
        r = send(h, [CMD, GET_INDICATOR, i])
        preset["indicators"].append({
            "enabled": bool(r[3]),
            "color": f"#{r[4]:02X}{r[5]:02X}{r[6]:02X}",
        })
    # Full dynamic keymap (all layers, row-major) as keycode names, so a preset
    # can reproduce key->TD (and any other) assignments on stock firmware.
    preset["keymap"] = [
        [name_of(via_get_keycode(h, l, rw, c)) for rw in range(ROWS) for c in range(COLS)]
        for l in range(LAYERS)
    ]
    return preset


def write_config(h, preset):
    """Apply a preset, writing only values that differ from the device (saves EEPROM)."""
    cur = read_config(h)
    n = 0

    def chg(label, old, new):
        nonlocal n
        if old != new:
            n += 1
        return old != new

    if chg("tt", cur["tapping_term"], preset["tapping_term"]):
        send(h, [CMD, SET_TT, preset["tapping_term"] & 0xFF, (preset["tapping_term"] >> 8) & 0xFF])
    for pid, key in ((PARAM_QUICKTAP, "quick_tap_term"), (PARAM_ASTIMEOUT, "autoshift_timeout"),
                     (PARAM_CWTIMEOUT, "caps_word_timeout"), (PARAM_DEBOUNCE, "debounce_time"),
                     (PARAM_ONESHOT, "oneshot_timeout")):
        if key in preset and chg(key, cur[key], preset[key]):
            set_param(h, pid, preset[key])
    if "debounce_method" in preset and chg("debounce_method", cur["debounce_method"], preset["debounce_method"]):
        set_param(h, PARAM_DEBOUNCE_METHOD, dbmethod_index(preset["debounce_method"]))
    for bit, key in enumerate(FLAG_KEYS):
        if chg(key, cur["flags"][key], preset["flags"].get(key, False)):
            set_flag(h, bit, preset["flags"].get(key, False))
    for i, td in enumerate(preset["tap_dance"][:TD_SLOT_COUNT]):
        c = cur["tap_dance"][i]
        if c["tap"] != td["tap"] or c["secondary"] != td["secondary"]:
            n += 1
            tap, sec = kc(td["tap"]), kc(td["secondary"])
            send(h, [CMD, SET_TD_KC, i, tap & 0xFF, tap >> 8, sec & 0xFF, sec >> 8])
        if chg("mode", c["mode"], td["mode"]):
            send(h, [CMD, SET_TD_MODE, i, 1 if td["mode"] == "hold" else 0])
        if chg("en", c["enabled"], td["enabled"]):
            send(h, [CMD, SET_TD_EN, i, 1 if td["enabled"] else 0])
    for i, cb in enumerate(preset.get("combos", [])[:COMBO_SLOT_COUNT]):
        if cur["combos"][i] != cb:
            n += 1
            set_combo(h, i, [kc(x) for x in cb["keys"]], kc(cb["output"]), cb.get("enabled", False))
    for i, ko in enumerate(preset.get("key_overrides", [])[:KO_SLOT_COUNT]):
        if cur["key_overrides"][i] != ko:
            n += 1
            set_ko(h, i, {
                "trigger": kc(ko["trigger"]), "replacement": kc(ko["replacement"]),
                "trigger_mods": modmask_from_list(ko.get("trigger_mods", [])),
                "suppressed_mods": modmask_from_list(ko.get("suppressed_mods", [])),
                "negative_mods": modmask_from_list(ko.get("negative_mods", [])),
                "layers": sum(1 << l for l in ko.get("layers", [])),
                "options": ko.get("options", 7), "enabled": ko.get("enabled", False),
            })
    for i, ind in enumerate(preset["indicators"][:len(IND_NAMES)]):
        c = cur["indicators"][i]
        if c["enabled"] != ind["enabled"] or c["color"].upper() != ind["color"].upper():
            n += 1
            r, g, b = parse_color([ind["color"]])
            set_indicator(h, i, ind["enabled"], (r, g, b))
    if "keymap" in preset:
        for l, layer in enumerate(preset["keymap"][:LAYERS]):
            for i, name in enumerate(layer[:ROWS * COLS]):
                if cur["keymap"][l][i] != name:
                    n += 1
                    via_set_keycode(h, l, i // COLS, i % COLS, kc(name))
    return n


def preset_path(name):
    return PRESET_DIR / (name + ".json")


def save_preset(h, name):
    PRESET_DIR.mkdir(exist_ok=True)
    preset = read_config(h)
    preset["name"] = name
    preset_path(name).write_text(json.dumps(preset, indent=2))
    print(f"Saved {preset_path(name)}")


def load_preset(h, name):
    p = preset_path(name)
    if not p.exists():
        sys.exit(f"No preset '{name}' at {p}. Available: {', '.join(list_presets()) or '(none)'}")
    preset = json.loads(p.read_text())
    n = write_config(h, preset)
    print(f"Loaded '{name}' ({n} change(s) written).")
    show_features(h)


def list_presets():
    if not PRESET_DIR.exists():
        return []
    return sorted(p.stem for p in PRESET_DIR.glob("*.json"))


def main():
    h = open_dev()
    a = sys.argv[1:]
    op = a[0] if a else "get"

    if op == "get":
        r = send(h, [CMD, GET_GLOBAL])
        count = r[4]
        enabled = int.from_bytes(bytes(r[5:13]), "little")
        mode = int.from_bytes(bytes(r[13:21]), "little")
        print(f"tapping_term={r[2] | (r[3] << 8)}  slots={count}")
        print(f"td_enabled=0x{enabled:016X}  td_mode=0x{mode:016X}")
    elif op == "tt":
        r = send(h, [CMD, SET_TT, int(a[1]) & 0xFF, (int(a[1]) >> 8) & 0xFF])
        print(f"tapping_term={r[2] | (r[3] << 8)}")
    elif op == "list":
        r = send(h, [CMD, GET_GLOBAL])
        for i in range(r[4]):
            show_slot(h, i)
    elif op == "show":
        show_slot(h, int(a[1]))
    elif op == "en":
        send(h, [CMD, SET_TD_EN, int(a[1]), int(a[2])])
        show_slot(h, int(a[1]))
    elif op == "mode":
        m = 1 if a[2].lower() in ("hold", "1") else 0
        send(h, [CMD, SET_TD_MODE, int(a[1]), m])
        show_slot(h, int(a[1]))
    elif op == "kc":
        i, tap, sec = int(a[1]), kc(a[2]), kc(a[3])
        send(h, [CMD, SET_TD_KC, i,
                 tap & 0xFF, (tap >> 8) & 0xFF, sec & 0xFF, (sec >> 8) & 0xFF])
        show_slot(h, i)
    elif op == "reset":
        send(h, [CMD, RESET])
        print("Defaults restored.")
    elif op == "keyget":
        l, rw, c = int(a[1]), int(a[2]), int(a[3])
        code = via_get_keycode(h, l, rw, c)
        print(f"L{l} R{rw} C{c} = {name_of(code)} (0x{code:04X})")
    elif op == "keyset":
        l, rw, c, code = int(a[1]), int(a[2]), int(a[3]), kc(a[4])
        via_set_keycode(h, l, rw, c, code)
        print(f"L{l} R{rw} C{c} -> {name_of(code)}")
    elif op == "assign":
        l, rw, c, slot = int(a[1]), int(a[2]), int(a[3]), int(a[4])
        via_set_keycode(h, l, rw, c, QK_TAP_DANCE | slot)
        print(f"L{l} R{rw} C{c} -> TD{slot}")
    elif op == "unassign":
        l, rw, c, code = int(a[1]), int(a[2]), int(a[3]), kc(a[4])
        via_set_keycode(h, l, rw, c, code)
        print(f"L{l} R{rw} C{c} -> {name_of(code)}")
    elif op == "dump":
        l = int(a[1])
        print(f"Layer {l}:      " + "".join(f"C{c:<6}" for c in range(COLS)))
        for rw in range(ROWS):
            cells = "".join(f"{name_of(via_get_keycode(h, l, rw, c)):<7}"
                            for c in range(COLS))
            print(f"  R{rw}        {cells}")
    elif op == "id":
        send(h, [CMD, IDENTIFY])  # ack
        print("Press the key you want to identify...")
        r = h.read(32, timeout_ms=15000)
        if not r or r[0] != CMD or r[1] != IDENTIFY:
            sys.exit("No keypress captured (timed out).")
        code = r[4] | (r[5] << 8)
        print(f"row={r[2]} col={r[3]}  current keycode={name_of(code)} (0x{code:04X})")
    elif op == "kmreset":
        send_raw(h, [VIA_KEYMAP_RESET])
        print("Whole keymap reset to compiled defaults.")
    elif op == "features":
        show_features(h)
    elif op == "capsword":
        set_flag(h, FF_CAPS_WORD, a[1]); show_features(h)
    elif op == "permissive":
        set_flag(h, FF_PERMISSIVE, a[1]); show_features(h)
    elif op == "hokp":
        set_flag(h, FF_HOKP, a[1]); show_features(h)
    elif op == "retro":
        set_flag(h, FF_RETRO, a[1]); show_features(h)
    elif op == "autoshift":
        set_flag(h, FF_AUTOSHIFT, a[1]); show_features(h)
    elif op == "dtapshift":
        set_flag(h, FF_CW_DOUBLE_SHIFT, a[1]); show_features(h)
    elif op == "bothshift":
        set_flag(h, FF_CW_BOTH_SHIFTS, a[1]); show_features(h)
    elif op == "cwtimeout":
        set_param(h, PARAM_CWTIMEOUT, a[1]); show_features(h)
    elif op == "quicktap":
        set_param(h, PARAM_QUICKTAP, a[1]); show_features(h)
    elif op == "astimeout":
        set_param(h, PARAM_ASTIMEOUT, a[1]); show_features(h)
    elif op == "debounce":
        set_param(h, PARAM_DEBOUNCE, a[1]); show_features(h)
    elif op == "dbmethod":
        set_param(h, PARAM_DEBOUNCE_METHOD, dbmethod_index(a[1])); show_features(h)
    elif op == "oneshot":
        if len(a) > 1:
            set_param(h, PARAM_ONESHOT, a[1])
        show_features(h)
    elif op == "combos":
        for i in range(COMBO_SLOT_COUNT):
            show_combo(h, i)
    elif op == "combo":
        i = int(a[1])
        if len(a) > 2 and a[2].lower() == "off":
            c = get_combo(h, i)
            set_combo(h, i, c["keys"], c["output"], False)
        else:
            # combo <i> <out> <k1> <k2> [k3] [k4]
            if len(a) < 5:
                sys.exit("usage: combo <i> off|<out> <k1> <k2> [k3] [k4]")
            out = kc(a[2])
            keys = [kc(x) for x in a[3:3 + COMBO_MAX_KEYS]]
            set_combo(h, i, keys, out, True)
        show_combo(h, i)
    elif op == "overrides":
        for i in range(KO_SLOT_COUNT):
            show_override(h, i)
    elif op == "override":
        i = int(a[1])
        if len(a) > 2 and a[2].lower() == "off":
            k = get_ko(h, i); k["enabled"] = False
            set_ko(h, i, k)
        else:
            # override <i> <trig> <repl> [mods] [layers]
            if len(a) < 4:
                sys.exit("usage: override <i> off|<trig> <repl> [mods] [layers]")
            mods = modmask_from_list(a[4].split("+")) if len(a) > 4 else 0
            layers = sum(1 << int(l) for l in a[5].split(",")) if len(a) > 5 else 0x0F
            set_ko(h, i, {
                "trigger": kc(a[2]), "replacement": kc(a[3]),
                "trigger_mods": mods, "suppressed_mods": 0, "negative_mods": 0,
                "layers": layers, "options": 7, "enabled": True,
            })
        show_override(h, i)
    elif op == "indicators":
        for i in range(len(IND_NAMES)):
            show_indicator(h, i)
    elif op == "indicator":
        i = ind_index(a[1])
        enabled = a[2].lower() in ("on", "1")
        if len(a) > 3:
            rgb = parse_color(a[3:])
        else:
            r = send(h, [CMD, GET_INDICATOR, i])  # keep stored color
            rgb = (r[4], r[5], r[6])
        set_indicator(h, i, enabled, rgb)
        show_indicator(h, i)
    elif op == "presets":
        names = list_presets()
        print("Presets in", PRESET_DIR)
        print("  " + ("  ".join(names) if names else "(none)"))
    elif op == "save":
        save_preset(h, a[1])
    elif op == "load":
        load_preset(h, a[1])
    elif op == "mine":
        load_preset(h, "mine")  # convenience alias for `load mine`
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
