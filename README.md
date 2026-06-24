# q1config

Live page: <https://opcow.github.io/q1config/>

Repository: <https://github.com/opcow/q1config>

Host-side configuration tools for a customized **Keychron Q1 Pro** (ANSI knob) running a
QMK `rtcfg` keymap that exposes a raw-HID runtime-config interface (command byte `0xAC`).
Change tap dance, tapping term, Caps Word, Auto Shift, combos, key overrides, key assignments,
macros, RGB lighting, and RGB state indicators **at runtime** — no recompile/reflash — and
save/load configurations as files.

> **Requires a one-time firmware flash.** These tools only work once the board is running the
> custom `rtcfg` firmware build (see the **Companion firmware** note below) — the
> stock Keychron firmware won't respond. The flash is a one-time step; after it, day-to-day
> changes are made live over USB with no further reflashing.
>
> **VIA still works.** The `rtcfg` build keeps full VIA compatibility — VIA/Vial can still
> connect and remap keys as usual. (Settings made through these tools live in the keyboard's
> own config and simply aren't shown in the VIA GUI; they don't interfere with it.)

Three front-ends over the same protocol:

- **`q1config`** — a native desktop GUI (C++ / Dear ImGui) with a graphical keyboard, a
  categorized keycode picker, tap-dance editor, combo and key-override editors, timing,
  indicator, lighting, and macro controls, and presets. The same binary doubles as a
  command-line tool when given arguments. No Python needed.
- **`q1config.html`** — a single-file browser GUI (WebHID; Chrome/Edge) with a graphical
  keyboard, key remapping, tap-dance editor, combo and key-override editors, sliders/toggles,
  color pickers, and lighting and macro editors, and presets.
- **`q1config.py`** — command-line tool (Python + `hidapi`).

> Companion firmware: the `rtcfg` keymap in the QMK tree —
> [opcow/qmk_firmware @ raw-hid-config](https://github.com/opcow/qmk_firmware/tree/raw-hid-config/keyboards/keychron/q1_pro/ansi_knob/keymaps/rtcfg)
> (note: the `q1_pro` board lives on the `raw-hid-config` branch, not `master`). This app
> only does anything once that firmware is flashed. The wire format is in [PROTOCOL.md](PROTOCOL.md).

**Adding this to another keyboard?** See [PORTING.md](PORTING.md) — how the firmware works
and step-by-step instructions for adding a compatible real-time-config interface to any QMK
board that lacks a Vial port.

## Screenshots

Native app:

![native app](images/ss-2.png)

Browser GUI:

![browser GUI](images/ss-1.png)

## Requirements

- **Windows, macOS, or Linux.** (WSL2 can't reach the device without usbipd.)
- For the **native app**: a C++17 compiler and **CMake ≥ 3.20**. Dependencies (GLFW, Dear
  ImGui, hidapi, nlohmann/json, nativefiledialog) are fetched automatically by CMake.
  On Linux also install `libudev-dev` and `libgtk-3-dev`.
- For the **Python CLI**: **Python 3** and `pip install -r requirements.txt` (just `hidapi`).
- For the **browser GUI**: a **Chromium browser** (Chrome/Edge) that supports WebHID.

## Native app

Build the single `q1config` binary (it is both the GUI and the CLI):

```powershell
cmake -B build
cmake --build build --config Release
```

Run it with **no arguments** to launch the desktop GUI:

```powershell
build\Release\q1config.exe        # Windows
./build/q1config                  # macOS/Linux
```

Connect the keyboard and use the **Keyboard / Features / Tap Dance / Timing / Combos /
Key Overrides / Indicators / Lighting / Macros** tabs; click any key to open the categorized
keycode picker. Save/Load presets use a native file dialog and share the same JSON format as
the other front-ends.

Pass a **command** to use the same binary as a CLI instead of opening the window:

```powershell
q1config features          # feature flags + timing params
q1config tt 220            # set tapping term (ms)
q1config td 64             # show tap-dance slots (default 8, max 64)
q1config indicators        # RGB indicator states
q1config keymap 4 dump.txt # dump 4 layers' keycodes to a file
q1config save work.json    # snapshot config to a JSON preset
q1config load work.json    # apply a preset
q1config reset             # config back to firmware defaults
q1config reset-keymap      # full keymap back to firmware defaults
```

Run `q1config help` for the full command list. (On Windows the GUI build is a windowed
binary; when run with a command it re-attaches to the parent console for output.)

## Python CLI

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

## Browser GUI

WebHID requires a secure context, so serve over `localhost` (a plain `file://` open won't work):

```powershell
python -m http.server 8000
```

Then open **<http://localhost:8000/q1config.html>** in Edge/Chrome, click **Connect**, and
authorize the keyboard. Save/Load presets use browser download / file picker; the JSON
format is identical to the other front-ends', so preset files are interchangeable.

## Presets

A preset is a full snapshot of the runtime config (timing, feature flags, all tap-dance
slots as keycode names, and indicator colors as hex) — human-readable and shareable.
`presets/mine.json` is the maintainer's personal setup; it's just a regular preset (load
it, edit it, or branch new ones with `save`). All three front-ends read and write the same
JSON, so presets are interchangeable between them.
