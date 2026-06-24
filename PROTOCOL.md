# Wire protocol

The firmware and this app must agree on these definitions. The authoritative source is the
shared rtcfg core (`keychron/rtcfg_common/rtcfg_common.c`, used by every Keychron `_max`
board; the Q1 Pro keymap `keychron/q1_pro/ansi_knob/keymaps/rtcfg/keymap.c` is the original
self-contained copy). This file mirrors them so the two repos stay in sync — update both
together when changing the protocol.

## Transport

- USB raw HID: **usage page `0xFF60`, usage `0x61`**. VID/PID are per board (Q1 Pro is
  **`0x3434`/`0x0610`**); each `_max` board has its own — see its `info.json`.
- 32-byte reports, report ID 0. Each request gets one 32-byte reply.
- Custom commands use a leading **`0xAC`** byte and are routed in firmware via
  `via_command_user()`. Standard VIA commands (no `0xAC`) are handled by `via.c`.

## `0xAC` commands

Request byte 0 = `0xAC`, byte 1 = subcommand. For `0xAC` replies, **byte 1 is a status**:
`0x00` = OK, `0xFF` = error.

| Sub | Name | Request bytes (after 0xAC) | Reply payload |
|----|------|-----|-----|
| 01 | GET_GLOBAL | — | `[2..3]` tapping_term, `[4]` slot_count(32), `[5..12]` td_enabled (LE u64), `[13..20]` td_mode (LE u64), `[21]` combo_count(16), `[22]` ko_count(16) |
| 02 | SET_TT | tt_lo, tt_hi | echoes global |
| 03 | SET_TD_EN | idx, en(0/1) | global |
| 04 | SET_TD_MODE | idx, mode(0=double,1=hold) | global |
| 05 | RESET | — | global (config reset to firmware defaults) |
| 06 | GET_TD | idx | `[2]` idx, `[3..4]` tap kc, `[5..6]` secondary kc, `[7]` enabled, `[8]` mode, `[9..10]` tapping_term (LE, 0=inherit global), `[11]` ph_flags |
| 07 | SET_TD_KC | idx, tap_lo, tap_hi, sec_lo, sec_hi | `[2]` idx |
| 08 | IDENTIFY | — | ack `[1]=00`; then an **unsolicited** report `[0]=AC,[1]=08,[2]=row,[3]=col,[4..5]=kc` on the next keypress (that press is consumed) |
| 09 | GET_FEATURES | — | `[2..3]` flags, `[4..5]` quick_tap_term, `[6..7]` autoshift_timeout, `[8..9]` caps_word_timeout, `[10]` debounce_time, `[11]` debounce_method, `[12..13]` oneshot_timeout |
| 0A | SET_FLAG | bit, val(0/1) | features |
| 0B | SET_PARAM | id(0=quicktap,1=astimeout,2=cwtimeout,3=debounce,4=debounce_method,5=oneshot_timeout), lo, hi | features |
| 0C | GET_INDICATOR | idx | `[2]` idx, `[3]` enabled, `[4]` r, `[5]` g, `[6]` b |
| 0D | SET_INDICATOR | idx, enabled, r, g, b | `[2]` idx |
| 0E | GET_COMBO | idx | `[2]` idx, `[3..10]` key0..3 (LE u16 each), `[11..12]` output (LE), `[13]` enabled |
| 0F | SET_COMBO | idx, key0..3 (LE u16 ×4), output (LE), enabled | `[2]` idx |
| 10 | GET_KO | idx | `[2]` idx, `[3..4]` trigger (LE), `[5..6]` replacement (LE), `[7]` trigger_mods, `[8]` suppressed_mods, `[9]` negative_mod_mask, `[10]` layers, `[11]` options, `[12]` enabled |
| 11 | SET_KO | idx, trig (LE), repl (LE), trig_mods, supp_mods, neg_mods, layers, options, enabled | `[2]` idx |
| 14 | SET_TD_TIMING | idx, tt_lo, tt_hi, ph_flags | `[2]` idx |

All multi-byte scalars are **little-endian**.

**feature_flags bits:** 0 caps_word, 1 permissive_hold, 2 hold_on_other_key, 3 retro_tapping,
4 auto_shift, 5 caps_word_double_shift (double-tap LShift → Caps Word), 6 caps_word_both_shifts
(hold both shifts → Caps Word). Bits 5/6 only act while caps_word (bit 0) is enabled.

**Indicators (idx):** 0 Caps Lock, 1 Caps Word, 2 Win FN layer, 3 Num Lock, 4 Scroll Lock,
5 Mac FN layer, 6 Windows mode (default layer = Win base), 7 one-shot mod armed. Only one
paints at a time, in that priority order (Caps Lock highest), each gated on its enabled flag.
"Windows mode" is persistent — it tints the whole board the entire time it's active. The FN/OS
layer indices are board-overridable (`RTCFG_FN_INDICATOR_LAYER` / `RTCFG_MAC_FN_INDICATOR_LAYER`
/ `RTCFG_OS_MODE_LAYER` in firmware `config.h`).

**Debounce methods (idx):** 0 none, 1 sym_defer_g (default), 2 sym_eager_pk,
3 asym_eager_defer_pk. A `debounce_time` of 0 means no debounce regardless of method.

**Tap-dance slots:** 32 total, named `TD0`–`TD31` (the name is the slot index). Slots 0–7 ship
with default keycodes; 8–31 start blank. The keycode that triggers slot *n* is `TD(n) = 0x5700 | n`.
GET_TD `ph_flags` bits: bit 0 (`TD_PH_VALUE`) = permissive-hold on/off value; bit 1 (`TD_HAS_PH`)
= whether PH is overridden for this slot. `0x00` = inherit global, `0x02` = override Off, `0x03` = override On.
SET_TD_TIMING `tapping_term = 0` means inherit global; `ph_flags` encoding is the same as GET_TD.

**One-shot timeout:** `oneshot_timeout` is ms; `0` = a one-shot mod/layer never auto-expires.
QMK's built-in `ONESHOT_TIMEOUT` is disabled and the keymap implements this at runtime
(tap-toggle count stays compile-time).

**Combos:** 16 slots. Each has up to 4 input keycodes (`KC_NO`-padded) and one output keycode;
a slot fires only when enabled and given ≥2 input keys. Inputs/outputs are normal keycodes.
The output may be any keycode, including `QK_BOOT` (`0x7C00`) — a combo with that output enters
the DFU bootloader (handy as a software "reset" chord for reflashing). Use a deliberate 3–4 key
chord to avoid accidental entry.

**Key overrides:** 16 slots. `trigger`+`replacement` are keycodes; `trigger_mods`,
`suppressed_mods`, `negative_mod_mask` are **8-bit modifier masks** (bit 0 LCtl, 1 LSft, 2 LAlt,
3 LGui, 4 RCtl, 5 RSft, 6 RAlt, 7 RGui); `layers` is a bitmask (bit *i* = layer *i*); `options`
is QMK's `ko_option_t` (use `0x07` = all activations). Only enabled slots are installed.

**Mod-tap / layer-tap / one-shot / modded keycodes** (placed via VIA or used as combo/override
keycodes) follow QMK encodings. Host tools render/parse these textual forms (mods are a
`+`-joined 5-bit set `LCtl/LSft/LAlt/LGui` or `RCtl/…`):
`MT(mods,KC) = 0x2000 | (mods<<8) | kc`, `LT(layer,KC) = 0x4000 | (layer<<8) | kc`,
`OSM(mods) = 0x52A0 | mods`, `OSL(layer) = 0x5280 | layer`, modded `MOD(mods,KC) = 0x0100..0x1FFF`.

## VIA dynamic-keymap commands (key remapping)

Standard VIA, handled by `via.c` (not `0xAC`). **Keycodes are big-endian** here, and reply
byte 1 is data (the layer), not a status.

| Cmd | Name | Bytes |
|----|------|-----|
| 04 | get_keycode | `[04, layer, row, col]` → `[04, layer, row, col, kc_hi, kc_lo]` |
| 05 | set_keycode | `[05, layer, row, col, kc_hi, kc_lo]` |
| 06 | keymap reset | `[06]` (whole keymap back to compiled defaults) |

Layers: 0 MAC_BASE, 1 MAC_FN, 2 WIN_BASE, 3 WIN_FN. Matrix is 6 rows × 16 cols.

## EEPROM / versioning

Config persists in QMK's user data block (600 bytes). `EECONFIG_USER_DATA_VERSION` in the
firmware's `config.h` (currently `0x00514412`) is bumped whenever the struct layout changes;
on the next flash the stored config is discarded and firmware defaults reapplied.

**One-time keymap reset:** growing the user block shifts the dynamic-keymap EEPROM base (it
starts right after the user block), so the stored VIA keymap moves. After flashing a build that
changes `EECONFIG_USER_DATA_SIZE`, run **Reset keymap** once to restore key assignments.

## Preset JSON schema

```json
{
  "name": "mine",
  "schema": 1,
  "tapping_term": 200,
  "quick_tap_term": 120,
  "autoshift_timeout": 175,
  "caps_word_timeout": 5000,
  "debounce_time": 5,
  "debounce_method": "sym_defer_g",
  "oneshot_timeout": 1000,
  "flags": { "caps_word": true, "permissive_hold": false, "hold_on_other_key": false,
             "retro_tapping": false, "auto_shift": false,
             "caps_word_double_shift": true, "caps_word_both_shifts": false },
  "tap_dance": [ { "tap": "NO", "secondary": "CAPS", "mode": "double", "enabled": true,
                   "tapping_term": 0, "permissive_hold": null }, … 32 ],
  "combos": [ { "keys": ["A","B","NO","NO"], "output": "ESC", "enabled": true }, … 16 ],
  "key_overrides": [ { "trigger": "1", "replacement": "F1", "trigger_mods": ["LSft"],
                      "suppressed_mods": [], "negative_mods": [], "layers": [0,2],
                      "options": 7, "enabled": true }, … 16 ],
  "indicators": [ { "enabled": true, "color": "#FF0000" }, … 8 ],
  "keymap": [ [ "ESC","F1", … 96 per layer (6×16, row-major) ], … 4 layers ]
}
```

Keycodes are names (e.g. `SCLN`, `COLN`, `F12`, `TD5`, `S(SCLN)`, `MT(LSft,A)`, `LT(1,SPC)`,
`OSM(LSft)`, `OSL(1)`, `MOD(LCtl,A)`, or raw `0x….`); modifier lists use `LCtl`,`LSft`,`LAlt`,
`LGui`,`RCtl`,`RSft`,`RAlt`,`RGui`; colors are `#rrggbb`. Loading writes only values that differ
from the live config (minimizes EEPROM writes).

**`tap_dance` timing fields:** `tapping_term: 0` means inherit global; `permissive_hold: null` means
no override (fall through to global `flags.permissive_hold`); `true`/`false` overrides for that slot.

**Stock firmware + `keymap`:** the firmware ships **stock** — a normal keymap (no tap-dance
assignments) and all tap-dance slots blank/disabled. Personal key→`TD` assignments live only in
the `keymap` field of a preset (all four layers, row-major over the 6×16 matrix). `save` captures
the live keymap; `load` rewrites only changed keys. The field is optional — presets without it
leave the keymap untouched. So a fresh flash (or `reset` + `reset-keymap`) is a plain keyboard,
and `load mine` reproduces the full personal setup including key assignments.
