# Porting real-time config to another QMK keyboard

This guide explains the firmware side of q1config and how to add a compatible
real-time-configuration interface to **another QMK keyboard that has no Vial port**. Once a
board speaks this protocol, the same `q1config.py` CLI and `q1config.html` WebHID GUI work
against it (after a VID/PID + layout tweak).

The reference implementation is the Keychron Q1 Pro `rtcfg` keymap. Exact byte layouts live
in [PROTOCOL.md](PROTOCOL.md); this document focuses on *how it's wired* and *what to copy*.

---

## 1. What this gives you / when to use it

A host app can read and write keyboard settings **at runtime, over USB**, with no recompile
or reflash: tapping term, per-key tap-hold options, tap-dance behavior, Caps Word, Auto
Shift, RGB state indicators, and even key remapping. Settings persist in EEPROM. It's a
lightweight, custom subset of what Vial provides — useful when your board isn't supported by
the vial-qmk fork.

Requirements on the target board:

- QMK with **`VIA_ENABLE = yes`** (recommended — it pulls in `RAW_ENABLE` and the dynamic
  keymap used for key remapping). Plain **`RAW_ENABLE = yes`** also works if you don't want
  VIA; you then lose the built-in key-remap path.
- Host side: Python 3 + `hidapi` for the CLI; a Chromium browser (Chrome/Edge) for the GUI.

---

## 2. How it works (the firmware changes)

**Transport.** Standard QMK raw HID: USB usage page `0xFF60`, usage `0x61`, fixed **32-byte**
reports, report ID 0. Every request gets one 32-byte reply.

**Custom command byte.** All custom packets start with a byte that doesn't collide with
anything else on the wire — the reference uses **`0xAC`**. `data[1]` is a subcommand; for our
replies `data[1]` is a status (`0x00` OK / `0xFF` error). See [PROTOCOL.md](PROTOCOL.md).

**Routing — the important bit.** When VIA is enabled, `quantum/via.c`'s `raw_hid_receive()`
calls **`via_command_kb(data, length)` first**, before VIA's own command switch. If it
returns `true`, VIA treats the command as fully handled (you must call `raw_hid_send`
yourself). `via_command_kb` is declared **weak** in via.c, so:

- On a **plain VIA board** (no keyboard-level `via_command_kb`), just define
  `via_command_kb()` in your keymap.
- On a board whose core **already defines** `via_command_kb` (e.g. Keychron handles `0xAA`
  BT-DFU / `0xAB` factory test), you can't add a second definition — instead add a weak
  `via_command_user()` to the keyboard `.c` and call it from the existing `default:` branch,
  then override `via_command_user()` in your keymap.
- With **no VIA**, define `raw_hid_receive()` directly.

**Storage.** Config lives in QMK's per-user EEPROM "data block" (`EECONFIG_USER_DATA_SIZE`).
A version dword (`EECONFIG_USER_DATA_VERSION`) guards the layout: bump it whenever the struct
changes and stale data is discarded so defaults reapply on the next boot.

**Responses & IDENTIFY.** Handlers mutate the 32-byte buffer and call `raw_hid_send`. One
command (IDENTIFY) acks immediately, then sends an *unsolicited* report with the next
keypress's row/col — handy for "click a key in the GUI."

**Key remapping for free.** Assigning a physical key to a tap-dance slot (or any keycode)
uses QMK's **standard VIA dynamic-keymap** commands (`0x05` set / `0x04` get), no custom
firmware needed — a tap-dance keycode is just `TD(n) = 0x5700 | n`.

**Neutral defaults.** The reference ships *stock* compile-time defaults (everything off) so
the firmware is shareable; personal setup is applied at runtime and stored only in the
owner's EEPROM. Recommended but optional.

---

## 3. Porting steps (minimal core)

### Step 0 — build flags

In your keymap's `rules.mk`:

```make
VIA_ENABLE = yes        # also enables RAW_ENABLE + dynamic keymap
# keep LTO_ENABLE off — it can break weak-symbol overrides across compile units
```

Pick a command byte that won't collide: avoid VIA's command IDs (`0x00`–`0x0D`) and any your
board already uses (Keychron uses `0xAA`/`0xAB`). The reference uses `0xAC`.

### Step 1 — route the command

**Plain VIA board** — in `keymap.c`:

```c
#include "raw_hid.h"
#define HID_CMD 0xAC

bool via_command_kb(uint8_t *data, uint8_t length) {
    if (data[0] != HID_CMD) return false;   // let VIA handle everything else
    handle_cmd(data, length);               // your dispatcher (Step 3)
    raw_hid_send(data, length);
    return true;                            // fully handled
}
```

**Board that already defines `via_command_kb`** (Keychron pattern) — in the keyboard `.c`,
add a weak hook and call it from the existing `default:`:

```c
__attribute__((weak)) bool via_command_user(uint8_t *data, uint8_t length) { return false; }

bool via_command_kb(uint8_t *data, uint8_t length) {
    switch (data[0]) {
        /* ...existing 0xAA / 0xAB cases... */
        default: return via_command_user(data, length);
    }
    return true;
}
```

…then implement `via_command_user()` (same body as the `via_command_kb` above) in `keymap.c`.

**No VIA** — define `raw_hid_receive()` directly and dispatch on `data[0]`.

### Step 2 — EEPROM config block

In `config.h`:

```c
#define EECONFIG_USER_DATA_SIZE    32      // sizeof(user_config_t), keep in sync
#define EECONFIG_USER_DATA_VERSION 0x0001  // bump on any struct-layout change
```

In `keymap.c`:

```c
typedef struct {
    uint16_t tapping_term;   // 0 = "uninitialized" sentinel
    /* ...add your fields; keep 32-bit fields first to avoid padding... */
    uint8_t  reserved[ /* pad to EECONFIG_USER_DATA_SIZE */ ];
} user_config_t;
_Static_assert(sizeof(user_config_t) == 32, "size must match EECONFIG_USER_DATA_SIZE");

static user_config_t user_config;
static const user_config_t default_config = { .tapping_term = 200, /* neutral defaults */ };

void eeconfig_init_user_datablock(void) {        // called on version mismatch / fresh EEPROM
    user_config = default_config;
    eeconfig_update_user_datablock(&user_config);
}

void keyboard_post_init_user(void) {
    eeconfig_read_user_datablock(&user_config);   // returns zeros if block is invalid
    if (user_config.tapping_term == 0) {          // sentinel ⇒ first run ⇒ defaults
        user_config = default_config;
        eeconfig_update_user_datablock(&user_config);
    }
}
```

### Step 3 — a minimal GET/SET handler

```c
enum { SUB_GET = 0x01, SUB_SET_TT = 0x02 };

static void handle_cmd(uint8_t *data, uint8_t length) {
    switch (data[1]) {
        case SUB_GET:
            data[1] = 0x00;                                   // OK
            data[2] = user_config.tapping_term & 0xFF;
            data[3] = user_config.tapping_term >> 8;
            break;
        case SUB_SET_TT:
            user_config.tapping_term = data[2] | (data[3] << 8);
            eeconfig_update_user_datablock(&user_config);
            data[1] = 0x00;
            break;
        default:
            data[1] = 0xFF;                                   // error
    }
}
```

Make the runtime value take effect — e.g. add the tapping-term callback (needs
`TAPPING_TERM_PER_KEY = yes`):

```c
uint16_t get_tapping_term(uint16_t kc, keyrecord_t *r) { return user_config.tapping_term; }
```

That's a complete, working real-time setting. Everything else is more of the same pattern.

---

## 4. Optional feature recipes

Each maps a `user_config` field (or bit) to a QMK hook; see the `rtcfg` keymap for full code.

- **Per-key tap-hold** — `#define PERMISSIVE_HOLD_PER_KEY` (etc.) in `config.h`, then return
  a runtime flag from `get_permissive_hold` / `get_hold_on_other_key_press` /
  `get_retro_tapping` / `get_quick_tap_term`.
- **Runtime tap-dance slots** — give every slot the same `ACTION_TAP_DANCE_FN_ADVANCED`
  callbacks; read keycodes/mode/enable from `user_config` (`user_data` = slot index). Place
  `TD(n)` in the layout for keys you want configurable.
- **Caps Word** — `#define CAPS_WORD_IDLE_TIMEOUT 0` to disable the built-in timer; gate
  activation in `caps_word_set_user(true)` (call `caps_word_off()` if disabled) and roll a
  runtime idle timeout in `housekeeping_task_user()`.
- **Auto Shift** — `AUTO_SHIFT_ENABLE = yes` + `#define AUTO_SHIFT_DISABLED_AT_STARTUP`;
  apply stored enable/timeout at boot with `autoshift_enable/disable` + `set_autoshift_timeout`
  (Auto Shift state isn't EEPROM-backed, so you own persistence).
- **Key Lock** — `KEY_LOCK_ENABLE = yes`; assign the `QK_LOCK` keycode to a key via the
  dynamic-keymap path. No stored config.
- **RGB state indicators** — store `{enabled, r, g, b}` per state and paint in
  `rgb_matrix_indicators_advanced_user()`.

---

## 5. Adapting the host app

The protocol is board-independent, so the CLI and GUI port with two changes:

1. **VID/PID** — set `VID`/`PID` (`q1config.py`) and the WebHID filter (`q1config.html`) to
   your board's USB IDs (from its `info.json` / `config.h`).
2. **Layout** (GUI only) — replace the `LAYOUT` array in `q1config.html` with your board's
   key coordinates from its `info.json` `layouts[...]layout` (each `{matrix:[r,c], x, y, w}`).

Drop any subcommands/UI for features you didn't implement. The preset JSON schema is
unchanged.

---

## 6. Gotchas

- **EEPROM reset:** changing `EECONFIG_USER_DATA_SIZE`/`_VERSION` (or first install) discards
  stored settings on the next flash — expected; reapply via a preset.
- **Command-byte collisions:** don't reuse VIA IDs `0x00`–`0x0D` or board-specific bytes.
- **Framing:** always 32-byte reports, report ID 0. The host pads to 32 and (hidapi) prepends
  a `0x00` report-ID byte.
- **One request at a time:** serialize host requests; each command expects exactly one reply.
- **VIA reply convention differs:** for VIA dynamic-keymap commands the reply's `data[1]` is
  data (the layer), *not* a status — don't status-check those.
- **Weak symbols + LTO:** keep `LTO_ENABLE` off; LTO can interfere with overriding weak
  functions across compilation units.
- **WebHID:** the GUI must be served from `http://localhost` (or https); `file://` is blocked.

---

See [PROTOCOL.md](PROTOCOL.md) for the full command table and preset schema, and
[README.md](README.md) for using the tools.
