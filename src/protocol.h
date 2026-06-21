#pragma once
#include <cstdint>
#include "hid.h"

static constexpr uint8_t CMD = 0xAC;

// Total tap-dance slots (must match TD_SLOT_COUNT in the firmware keymap).
static constexpr int TD_SLOT_COUNT = 64;

// td_enabled/td_mode are also in GET_GLOBAL but unused here (read per-slot via
// getTd); they're 64-bit on the wire now, so we don't decode them.
struct GlobalState { uint16_t tt; uint8_t slots; };
struct FeatState   { uint16_t flags; uint16_t quicktap; uint16_t astimeout; uint16_t cwtimeout; uint16_t debounce; uint8_t debounceMethod; };
struct TdSlot      { uint16_t tap; uint16_t sec; bool enabled; uint8_t mode; }; // mode: 0=double 1=hold
struct IndState    { bool enabled; uint8_t r, g, b; };

// Debounce method index <-> canonical name (must match keymap.c dispatcher order:
// 0 none, 1 sym_defer_g, 2 sym_eager_pk, 3 asym_eager_defer_pk).
const char* dbMethodName(uint8_t idx);  // "?" if out of range
int         dbMethodIndex(const char* name);  // -1 if unknown

// 0xAC commands
GlobalState getGlobal(HidDevice& d);
void        setTT(HidDevice& d, uint16_t ms);
void        setTdEn(HidDevice& d, int idx, bool on);
void        setTdMode(HidDevice& d, int idx, uint8_t mode);
void        resetCfg(HidDevice& d);
TdSlot      getTd(HidDevice& d, int idx);
void        setTdKc(HidDevice& d, int idx, uint16_t tap, uint16_t sec);
void        startIdentify(HidDevice& d);
FeatState   getFeat(HidDevice& d);
void        setFlag(HidDevice& d, int bit, bool on);
void        setParam(HidDevice& d, uint8_t pid, uint16_t val);
IndState    getInd(HidDevice& d, int idx);
void        setInd(HidDevice& d, int idx, bool en, uint8_t r, uint8_t g, uint8_t b);

// VIA dynamic keymap (big-endian keycodes, layer 0–3, 6 rows × 16 cols)
uint16_t viaGet(HidDevice& d, int layer, int row, int col);
void     viaSet(HidDevice& d, int layer, int row, int col, uint16_t kc);
void     viaKmReset(HidDevice& d);
