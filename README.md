# q1config

Repository: <https://github.com/opcow/q1config>

Host-side configuration tools for a customized **Keychron Q1 Pro** (ANSI knob) running a
QMK `rtcfg` keymap that exposes a raw-HID runtime-config interface (command byte `0xAC`).
Change tap dance, tapping term, Caps Word, Auto Shift, key assignments, and RGB state
indicators **at runtime** — no recompile/reflash — and save/load configurations as files.

Two front-ends over the same protocol:

- **`q1config.py`** — command-line tool (Python + `hidapi`).
- **`q1config.html`** — a single-file browser GUI (WebHID; Chrome/Edge) with a graphical
  keyboard, key remapping, tap-dance editor, sliders/toggles, color pickers, and presets.

> Companion firmware: the `rtcfg` keymap in the QMK tree —
> [opcow/qmk_firmware @ raw-hid-config](https://github.com/opcow/qmk_firmware/tree/raw-hid-config/keyboards/keychron/q1_pro/ansi_knob/keymaps/rtcfg)
> (note: the `q1_pro` board lives on the `raw-hid-config` branch, not `master`). This app
> only does anything once that firmware is flashed. The wire format is in [PROTOCOL.md](PROTOCOL.md).

**Adding this to another keyboard?** See [PORTING.md](PORTING.md) — how the firmware works
and step-by-step instructions for adding a compatible real-time-config interface to any QMK
board that lacks a Vial port.

## Requirements

- **Windows** (the CLI uses `hidapi` for USB access; WSL2 can't reach the device without usbipd).
- **Python 3** and `pip install -r requirements.txt` (just `hidapi`).
- A **Chromium browser** (Chrome/Edge) for the GUI (WebHID).

## CLI usage

```powershell
python q1config.py            # show global config
python q1config.py list       # all tap-dance slots
python q1config.py tt 220     # set tapping term (ms)
python q1config.py mode 3 hold; python q1config.py en 3 1   # ;/: tap-hold
python q1config.py indicator capslock on #ff0000            # red Caps Lock
python q1config.py id          # press a key -> prints its row/col
python q1config.py assign 2 3 10 5   # make a key trigger tap-dance slot 5

# presets (JSON files in ./presets/)
python q1config.py presets    # list
python q1config.py save work  # snapshot current config -> presets/work.json
python q1config.py load mine  # apply a preset (writes only what differs)
python q1config.py mine       # alias for `load mine`
```

Run `python q1config.py help` (or any unknown command) to see the full command list.

## GUI usage

WebHID requires a secure context, so serve over `localhost` (a plain `file://` open won't work):

```powershell
python -m http.server 8000
```

Then open **<http://localhost:8000/q1config.html>** in Edge/Chrome, click **Connect**, and
authorize the keyboard. Save/Load presets use browser download / file picker; the JSON
format is identical to the CLI's, so files are interchangeable between the two.

## Presets

A preset is a full snapshot of the runtime config (timing, feature flags, all 32
tap-dance slots as keycode names, and indicator colors as hex) — human-readable and
shareable. `presets/mine.json` is the maintainer's personal setup; it's just a regular
preset (load it, edit it, or branch new ones with `save`).

## Screenshot

![screenshot](images/ss-1.png)
