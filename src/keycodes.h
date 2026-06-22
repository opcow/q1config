#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

std::string nameOf(uint16_t code);
uint16_t    kcParse(const std::string& s);  // throws std::runtime_error on unknown
const std::vector<std::pair<std::string, uint16_t>>& allKeycodes();

// QMK keycode-range bases for the mod-tap / layer-tap / one-shot / modded builder.
static constexpr uint16_t QK_MODS      = 0x0100;  // ..0x1FFF  C()/S()/A()/G()
static constexpr uint16_t QK_MOD_TAP   = 0x2000;  // ..0x3FFF  MT()
static constexpr uint16_t QK_LAYER_TAP = 0x4000;  // ..0x4FFF  LT()
static constexpr uint16_t QK_OSM       = 0x52A0;  // ..0x52BF  OSM()

// 5-bit packed mod field used by MT()/OSM()/modded keycodes: bits 0-3 = Ctrl,
// Shift, Alt, Gui; bit 4 = right-hand. Round-trips with the "+"-joined token form
// (e.g. "LSFT+LCTL", or "0" for none); parsing is case-insensitive.
std::string mods5ToStr(uint8_t mods);
uint8_t     mods5FromStr(const std::string& s);  // throws on unknown token

// Keycodes grouped into VIA-style categories (Basic, Media, Layers, ...).
struct KcCategory {
    std::string name;
    std::vector<std::pair<std::string, uint16_t>> entries;
};
const std::vector<KcCategory>& keycodeCategories();
