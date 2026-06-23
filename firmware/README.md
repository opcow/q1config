# rtcfg firmware

Prebuilt QMK firmware with the **runtime-config (rtcfg)** interface that the
[q1config](../README.md) tools (CLI + WebHID GUI) drive: tap dance, combos, key
overrides, tap-hold timing, Caps Word, Auto Shift, one-shot, debounce, and RGB
state indicators — all configurable live over USB, no reflash. See
[PORTING.md](../PORTING.md) for how it works and [PROTOCOL.md](../PROTOCOL.md)
for the wire protocol.

## Flashing

These are STM32 `.bin` images. Put the board in bootloader/DFU mode (hold **Esc**
while plugging in, or press the reset key under the spacebar / on the PCB), then
flash with [QMK Toolbox](https://github.com/qmk/qmk_toolbox) or `dfu-util`
(load address `0x08000000`). Match the **exact** variant for your board — a
mismatched image is recoverable (re-enter DFU and reflash) but won't work right.
After first flash, run **Reset keymap** once in the host app (the larger config
block shifts the stored keymap; see PROTOCOL.md → EEPROM/versioning).

---

## ✅ Confirmed (hardware-tested)

Tested on real hardware and working.

| Model | Layout | Backlight | Knob | Firmware |
|-------|--------|-----------|------|----------|
| Q1 Pro | ANSI | RGB | Yes | [`.bin`](keychron_q1_pro_ansi_knob_rtcfg.bin) |

---

## 🧪 Experimental (compile-verified, **untested on hardware**)

Built from each board's stock layout over the shared rtcfg core and verified to
compile + link, but **not yet tested on physical hardware**. They should work,
but flash at your own risk and please report back so a board can graduate to
Confirmed. 97 images across 33 models.

### Max series

| Model | Layout | Backlight | Knob | Firmware |
|-------|--------|-----------|------|----------|
| K1 Max | ANSI | RGB | — | [`.bin`](experimental/keychron_k1_max_ansi_rgb_rtcfg.bin) |
| K1 Max | ANSI | White | — | [`.bin`](experimental/keychron_k1_max_ansi_white_rtcfg.bin) |
| K3 Max | ANSI | RGB | — | [`.bin`](experimental/keychron_k3_max_ansi_rgb_rtcfg.bin) |
| K3 Max | ANSI | White | — | [`.bin`](experimental/keychron_k3_max_ansi_white_rtcfg.bin) |
| K3 Max | ISO | RGB | — | [`.bin`](experimental/keychron_k3_max_iso_rgb_rtcfg.bin) |
| K3 Max | ISO | White | — | [`.bin`](experimental/keychron_k3_max_iso_white_rtcfg.bin) |
| K3 Max | JIS | RGB | — | [`.bin`](experimental/keychron_k3_max_jis_rgb_rtcfg.bin) |
| K3 Max | JIS | White | — | [`.bin`](experimental/keychron_k3_max_jis_white_rtcfg.bin) |
| K5 Max | ANSI | RGB | — | [`.bin`](experimental/keychron_k5_max_ansi_rgb_rtcfg.bin) |
| K5 Max | ANSI | White | — | [`.bin`](experimental/keychron_k5_max_ansi_white_rtcfg.bin) |
| K7 Max | ANSI | RGB | — | [`.bin`](experimental/keychron_k7_max_ansi_rgb_rtcfg.bin) |
| K7 Max | ANSI | White | — | [`.bin`](experimental/keychron_k7_max_ansi_white_rtcfg.bin) |
| Q1 Max | ANSI | — | Yes | [`.bin`](experimental/keychron_q1_max_ansi_encoder_rtcfg.bin) |
| Q1 Max | ISO | — | Yes | [`.bin`](experimental/keychron_q1_max_iso_encoder_rtcfg.bin) |
| Q1 Max | JIS | — | Yes | [`.bin`](experimental/keychron_q1_max_jis_encoder_rtcfg.bin) |
| Q2 Max | ANSI | — | Yes | [`.bin`](experimental/keychron_q2_max_ansi_encoder_rtcfg.bin) |
| Q2 Max | ISO | — | Yes | [`.bin`](experimental/keychron_q2_max_iso_encoder_rtcfg.bin) |
| Q3 Max | ANSI | — | Yes | [`.bin`](experimental/keychron_q3_max_ansi_encoder_rtcfg.bin) |
| Q5 Max | ANSI | — | Yes | [`.bin`](experimental/keychron_q5_max_ansi_encoder_rtcfg.bin) |
| Q6 Max | ANSI | — | Yes | [`.bin`](experimental/keychron_q6_max_ansi_encoder_rtcfg.bin) |
| Q60 Max | ANSI | — | — | [`.bin`](experimental/keychron_q60_max_ansi_rtcfg.bin) |
| Q65 Max | ANSI | — | Yes | [`.bin`](experimental/keychron_q65_max_ansi_encoder_rtcfg.bin) |
| Q8 Max | ANSI | — | Yes | [`.bin`](experimental/keychron_q8_max_ansi_encoder_rtcfg.bin) |
| V1 Max | ANSI | — | Yes | [`.bin`](experimental/keychron_v1_max_ansi_encoder_rtcfg.bin) |
| V1 Max | ISO | — | Yes | [`.bin`](experimental/keychron_v1_max_iso_encoder_rtcfg.bin) |
| V2 Max | ANSI | — | Yes | [`.bin`](experimental/keychron_v2_max_ansi_encoder_rtcfg.bin) |
| V2 Max | ISO | — | Yes | [`.bin`](experimental/keychron_v2_max_iso_encoder_rtcfg.bin) |
| V5 Max | ANSI | — | Yes | [`.bin`](experimental/keychron_v5_max_ansi_encoder_rtcfg.bin) |

### Pro series

| Model | Layout | Backlight | Knob | Firmware |
|-------|--------|-----------|------|----------|
| K1 Pro | ANSI | RGB | — | [`.bin`](experimental/keychron_k1_pro_ansi_rgb_rtcfg.bin) |
| K1 Pro | ANSI | White | — | [`.bin`](experimental/keychron_k1_pro_ansi_white_rtcfg.bin) |
| K1 Pro | ISO | RGB | — | [`.bin`](experimental/keychron_k1_pro_iso_rgb_rtcfg.bin) |
| K1 Pro | ISO | White | — | [`.bin`](experimental/keychron_k1_pro_iso_white_rtcfg.bin) |
| K10 Pro | ANSI | RGB | — | [`.bin`](experimental/keychron_k10_pro_ansi_rgb_rtcfg.bin) |
| K10 Pro | ANSI | White | — | [`.bin`](experimental/keychron_k10_pro_ansi_white_rtcfg.bin) |
| K10 Pro | ISO | RGB | — | [`.bin`](experimental/keychron_k10_pro_iso_rgb_rtcfg.bin) |
| K10 Pro | ISO | White | — | [`.bin`](experimental/keychron_k10_pro_iso_white_rtcfg.bin) |
| K10 Pro | JIS | RGB | — | [`.bin`](experimental/keychron_k10_pro_jis_rgb_rtcfg.bin) |
| K10 Pro | JIS | White | — | [`.bin`](experimental/keychron_k10_pro_jis_white_rtcfg.bin) |
| K11 Pro | ANSI | RGB | Yes | [`.bin`](experimental/keychron_k11_pro_ansi_encoder_rgb_rtcfg.bin) |
| K11 Pro | ANSI | White | Yes | [`.bin`](experimental/keychron_k11_pro_ansi_encoder_white_rtcfg.bin) |
| K11 Pro | ANSI | RGB | — | [`.bin`](experimental/keychron_k11_pro_ansi_rgb_rtcfg.bin) |
| K11 Pro | ANSI | White | — | [`.bin`](experimental/keychron_k11_pro_ansi_white_rtcfg.bin) |
| K13 Pro | ANSI | RGB | — | [`.bin`](experimental/keychron_k13_pro_ansi_rgb_rtcfg.bin) |
| K13 Pro | ANSI | White | — | [`.bin`](experimental/keychron_k13_pro_ansi_white_rtcfg.bin) |
| K15 Pro | ANSI | RGB | Yes | [`.bin`](experimental/keychron_k15_pro_ansi_encoder_rgb_rtcfg.bin) |
| K2 Pro | ANSI | RGB | — | [`.bin`](experimental/keychron_k2_pro_ansi_rgb_rtcfg.bin) |
| K2 Pro | ANSI | White | — | [`.bin`](experimental/keychron_k2_pro_ansi_white_rtcfg.bin) |
| K2 Pro | ISO | RGB | — | [`.bin`](experimental/keychron_k2_pro_iso_rgb_rtcfg.bin) |
| K2 Pro | ISO | White | — | [`.bin`](experimental/keychron_k2_pro_iso_white_rtcfg.bin) |
| K2 Pro | JIS | RGB | — | [`.bin`](experimental/keychron_k2_pro_jis_rgb_rtcfg.bin) |
| K2 Pro | JIS | White | — | [`.bin`](experimental/keychron_k2_pro_jis_white_rtcfg.bin) |
| K3 Pro | ANSI | RGB | — | [`.bin`](experimental/keychron_k3_pro_ansi_rgb_rtcfg.bin) |
| K3 Pro | ANSI | White | — | [`.bin`](experimental/keychron_k3_pro_ansi_white_rtcfg.bin) |
| K3 Pro | ISO | RGB | — | [`.bin`](experimental/keychron_k3_pro_iso_rgb_rtcfg.bin) |
| K3 Pro | ISO | White | — | [`.bin`](experimental/keychron_k3_pro_iso_white_rtcfg.bin) |
| K3 Pro | JIS | RGB | — | [`.bin`](experimental/keychron_k3_pro_jis_rgb_rtcfg.bin) |
| K3 Pro | JIS | White | — | [`.bin`](experimental/keychron_k3_pro_jis_white_rtcfg.bin) |
| K4 Pro | ANSI | RGB | — | [`.bin`](experimental/keychron_k4_pro_ansi_rgb_rtcfg.bin) |
| K4 Pro | ANSI | White | — | [`.bin`](experimental/keychron_k4_pro_ansi_white_rtcfg.bin) |
| K4 Pro | ISO | RGB | — | [`.bin`](experimental/keychron_k4_pro_iso_rgb_rtcfg.bin) |
| K4 Pro | ISO | White | — | [`.bin`](experimental/keychron_k4_pro_iso_white_rtcfg.bin) |
| K5 Pro | ANSI | RGB | — | [`.bin`](experimental/keychron_k5_pro_ansi_rgb_rtcfg.bin) |
| K5 Pro | ANSI | White | — | [`.bin`](experimental/keychron_k5_pro_ansi_white_rtcfg.bin) |
| K5 Pro | ISO | RGB | — | [`.bin`](experimental/keychron_k5_pro_iso_rgb_rtcfg.bin) |
| K5 Pro | ISO | White | — | [`.bin`](experimental/keychron_k5_pro_iso_white_rtcfg.bin) |
| K5 Pro | JIS | RGB | — | [`.bin`](experimental/keychron_k5_pro_jis_rgb_rtcfg.bin) |
| K5 Pro | JIS | White | — | [`.bin`](experimental/keychron_k5_pro_jis_white_rtcfg.bin) |
| K7 Pro | ANSI | RGB | — | [`.bin`](experimental/keychron_k7_pro_ansi_rgb_rtcfg.bin) |
| K7 Pro | ANSI | White | — | [`.bin`](experimental/keychron_k7_pro_ansi_white_rtcfg.bin) |
| K7 Pro | ISO | RGB | — | [`.bin`](experimental/keychron_k7_pro_iso_rgb_rtcfg.bin) |
| K8 Pro | ANSI | RGB | — | [`.bin`](experimental/keychron_k8_pro_ansi_rgb_rtcfg.bin) |
| K8 Pro | ANSI | White | — | [`.bin`](experimental/keychron_k8_pro_ansi_white_rtcfg.bin) |
| K8 Pro | ISO | RGB | — | [`.bin`](experimental/keychron_k8_pro_iso_rgb_rtcfg.bin) |
| K8 Pro | ISO | White | — | [`.bin`](experimental/keychron_k8_pro_iso_white_rtcfg.bin) |
| K8 Pro | JIS | RGB | — | [`.bin`](experimental/keychron_k8_pro_jis_rgb_rtcfg.bin) |
| K8 Pro | JIS | White | — | [`.bin`](experimental/keychron_k8_pro_jis_white_rtcfg.bin) |
| K9 Pro | ANSI | RGB | — | [`.bin`](experimental/keychron_k9_pro_ansi_rgb_rtcfg.bin) |
| K9 Pro | ANSI | White | — | [`.bin`](experimental/keychron_k9_pro_ansi_white_rtcfg.bin) |
| K9 Pro | ISO | RGB | — | [`.bin`](experimental/keychron_k9_pro_iso_rgb_rtcfg.bin) |
| K9 Pro | ISO | White | — | [`.bin`](experimental/keychron_k9_pro_iso_white_rtcfg.bin) |
| K9 Pro | JIS | RGB | — | [`.bin`](experimental/keychron_k9_pro_jis_rgb_rtcfg.bin) |
| K9 Pro | JIS | White | — | [`.bin`](experimental/keychron_k9_pro_jis_white_rtcfg.bin) |
| Q14 Pro | ANSI | — | Yes | [`.bin`](experimental/keychron_q14_pro_ansi_encoder_rtcfg.bin) |
| Q14 Pro | ISO | — | Yes | [`.bin`](experimental/keychron_q14_pro_iso_encoder_rtcfg.bin) |
| Q2 Pro | ANSI | — | Yes | [`.bin`](experimental/keychron_q2_pro_ansi_encoder_rtcfg.bin) |
| Q2 Pro | ISO | — | Yes | [`.bin`](experimental/keychron_q2_pro_iso_encoder_rtcfg.bin) |
| Q3 Pro | ANSI | — | Yes | [`.bin`](experimental/keychron_q3_pro_ansi_encoder_rtcfg.bin) |
| Q3 Pro | ANSI (SE) | — | Yes | [`.bin`](experimental/keychron_q3_pro_ansi_encoder_se_rtcfg.bin) |
| Q3 Pro | ISO | — | Yes | [`.bin`](experimental/keychron_q3_pro_iso_encoder_rtcfg.bin) |
| Q3 Pro | ISO (SE) | — | Yes | [`.bin`](experimental/keychron_q3_pro_iso_encoder_se_rtcfg.bin) |
| Q3 Pro | JIS (SE) | — | Yes | [`.bin`](experimental/keychron_q3_pro_jis_encoder_se_rtcfg.bin) |
| Q5 Pro | ANSI | — | Yes | [`.bin`](experimental/keychron_q5_pro_ansi_encoder_rtcfg.bin) |
| Q5 Pro | ISO | — | Yes | [`.bin`](experimental/keychron_q5_pro_iso_encoder_rtcfg.bin) |
| Q6 Pro | ANSI | — | Yes | [`.bin`](experimental/keychron_q6_pro_ansi_encoder_rtcfg.bin) |
| Q6 Pro | ISO | — | Yes | [`.bin`](experimental/keychron_q6_pro_iso_encoder_rtcfg.bin) |
| Q8 Pro | ANSI | — | Yes | [`.bin`](experimental/keychron_q8_pro_ansi_encoder_rtcfg.bin) |
| Q8 Pro | ISO | — | Yes | [`.bin`](experimental/keychron_q8_pro_iso_encoder_rtcfg.bin) |

---

### Not available

- **K6 Pro, K12 Pro, K14 Pro, Q4 Pro** — their 2&nbsp;KB EEPROM can't hold the
  rtcfg config block alongside the dynamic keymap.
- **K7 Pro ISO white, K15 Pro ANSI-encoder white** — broken upstream in this
  firmware branch (stock keymaps don't build either).
