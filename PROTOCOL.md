# Wire protocol

The firmware and this app must agree on these definitions. The authoritative source is the
QMK keymap (`keychron/q1_pro/ansi_knob/keymaps/rtcfg/keymap.c`); this file mirrors it so the
two repos stay in sync. Update both together when changing the protocol.

## Transport

- USB raw HID: **usage page `0xFF60`, usage `0x61`**, VID **`0x3434`**, PID **`0x0610`**.
- 32-byte reports, report ID 0. Each request gets one 32-byte reply.
- Custom commands use a leading **`0xAC`** byte and are routed in firmware via
  `via_command_user()`. Standard VIA commands (no `0xAC`) are handled by `via.c`.

## `0xAC` commands

Request byte 0 = `0xAC`, byte 1 = subcommand. For `0xAC` replies, **byte 1 is a status**:
`0x00` = OK, `0xFF` = error.

| Sub | Name | Request bytes (after 0xAC) | Reply payload |
|----|------|-----|-----|
| 01 | GET_GLOBAL | — | `[2..3]` tapping_term, `[4]` slot_count(64), `[5..12]` td_enabled (LE u64), `[13..20]` td_mode (LE u64) |
| 02 | SET_TT | tt_lo, tt_hi | echoes global |
| 03 | SET_TD_EN | idx, en(0/1) | global |
| 04 | SET_TD_MODE | idx, mode(0=double,1=hold) | global |
| 05 | RESET | — | global (config reset to firmware defaults) |
| 06 | GET_TD | idx | `[2]` idx, `[3..4]` tap kc, `[5..6]` secondary kc, `[7]` enabled, `[8]` mode |
| 07 | SET_TD_KC | idx, tap_lo, tap_hi, sec_lo, sec_hi | `[2]` idx |
| 08 | IDENTIFY | — | ack `[1]=00`; then an **unsolicited** report `[0]=AC,[1]=08,[2]=row,[3]=col,[4..5]=kc` on the next keypress (that press is consumed) |
| 09 | GET_FEATURES | — | `[2..3]` flags, `[4..5]` quick_tap_term, `[6..7]` autoshift_timeout, `[8..9]` caps_word_timeout |
| 0A | SET_FLAG | bit, val(0/1) | features |
| 0B | SET_PARAM | id(0=quicktap,1=astimeout,2=cwtimeout), lo, hi | features |
| 0C | GET_INDICATOR | idx | `[2]` idx, `[3]` enabled, `[4]` r, `[5]` g, `[6]` b |
| 0D | SET_INDICATOR | idx, enabled, r, g, b | `[2]` idx |

All multi-byte scalars are **little-endian**.

**feature_flags bits:** 0 caps_word, 1 permissive_hold, 2 hold_on_other_key, 3 retro_tapping,
4 auto_shift.

**Indicators (idx):** 0 Caps Lock, 1 Caps Word, 2 WIN_FN layer.

**Tap-dance slots:** 64 total (0–7 named, 8–63 generic). The keycode that triggers slot *n*
is `TD(n) = 0x5700 | n`.

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

Config persists in QMK's user data block (296 bytes). `EECONFIG_USER_DATA_VERSION` in the
firmware's `config.h` (currently `0x00514405`) is bumped whenever the struct layout changes;
on the next flash the stored config is discarded and firmware defaults reapplied.

## Preset JSON schema

```json
{
  "name": "mine",
  "schema": 1,
  "tapping_term": 200,
  "quick_tap_term": 120,
  "autoshift_timeout": 175,
  "caps_word_timeout": 5000,
  "flags": { "caps_word": true, "permissive_hold": false, "hold_on_other_key": false,
             "retro_tapping": false, "auto_shift": false },
  "tap_dance": [ { "tap": "NO", "secondary": "CAPS", "mode": "double", "enabled": true }, … 64 ],
  "indicators": [ { "enabled": true, "color": "#FF0000" }, … 3 ]
}
```

Keycodes are names (e.g. `SCLN`, `COLN`, `F12`, `TD5`, `S(SCLN)`, or raw `0x….`); colors are
`#rrggbb`. Loading writes only values that differ from the live config (minimizes EEPROM writes).
