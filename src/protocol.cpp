#include "protocol.h"
#include <cstring>

static const char* const DB_METHODS[] = {"none", "sym_defer_g", "sym_eager_pk", "asym_eager_defer_pk"};
static constexpr int DB_METHOD_COUNT = 4;

const char* dbMethodName(uint8_t idx) {
    return (idx < DB_METHOD_COUNT) ? DB_METHODS[idx] : "?";
}
int dbMethodIndex(const char* name) {
    for (int i = 0; i < DB_METHOD_COUNT; i++)
        if (!strcmp(name, DB_METHODS[i])) return i;
    return -1;
}

static uint16_t u16(const std::array<uint8_t,32>& d, int o) {
    return (uint16_t)(d[o] | (d[o+1] << 8));
}

GlobalState getGlobal(HidDevice& d) {
    auto r = d.xfer({CMD, 0x01});
    return {u16(r,2), r[4]};
}
void setTT(HidDevice& d, uint16_t ms) {
    d.xfer({CMD, 0x02, (uint8_t)(ms & 0xFF), (uint8_t)(ms >> 8)});
}
void setTdEn(HidDevice& d, int i, bool on) {
    d.xfer({CMD, 0x03, (uint8_t)i, (uint8_t)(on ? 1 : 0)});
}
void setTdMode(HidDevice& d, int i, uint8_t m) {
    d.xfer({CMD, 0x04, (uint8_t)i, m});
}
void resetCfg(HidDevice& d) { d.xfer({CMD, 0x05}); }

TdSlot getTd(HidDevice& d, int i) {
    auto r = d.xfer({CMD, 0x06, (uint8_t)i});
    return {u16(r,3), u16(r,5), (bool)r[7], r[8]};
}
void setTdKc(HidDevice& d, int i, uint16_t tap, uint16_t sec) {
    d.xfer({CMD, 0x07, (uint8_t)i,
            (uint8_t)(tap & 0xFF), (uint8_t)(tap >> 8),
            (uint8_t)(sec & 0xFF), (uint8_t)(sec >> 8)});
}
void startIdentify(HidDevice& d) { d.xfer({CMD, 0x08}); }

FeatState getFeat(HidDevice& d) {
    auto r = d.xfer({CMD, 0x09});
    return {u16(r,2), u16(r,4), u16(r,6), u16(r,8), r[10], r[11]};
}
void setFlag(HidDevice& d, int bit, bool on) {
    d.xfer({CMD, 0x0A, (uint8_t)bit, (uint8_t)(on ? 1 : 0)});
}
void setParam(HidDevice& d, uint8_t pid, uint16_t v) {
    d.xfer({CMD, 0x0B, pid, (uint8_t)(v & 0xFF), (uint8_t)(v >> 8)});
}
IndState getInd(HidDevice& d, int i) {
    auto r = d.xfer({CMD, 0x0C, (uint8_t)i});
    return {(bool)r[3], r[4], r[5], r[6]};
}
void setInd(HidDevice& d, int i, bool en, uint8_t r, uint8_t g, uint8_t b) {
    d.xfer({CMD, 0x0D, (uint8_t)i, (uint8_t)(en ? 1 : 0), r, g, b});
}

uint16_t viaGet(HidDevice& d, int l, int row, int col) {
    auto r = d.xfer({0x04, (uint8_t)l, (uint8_t)row, (uint8_t)col});
    return (uint16_t)((r[4] << 8) | r[5]);
}
void viaSet(HidDevice& d, int l, int row, int col, uint16_t kc) {
    d.xfer({0x05, (uint8_t)l, (uint8_t)row, (uint8_t)col,
            (uint8_t)(kc >> 8), (uint8_t)(kc & 0xFF)});
}
void viaKmReset(HidDevice& d) { d.xfer({0x06}); }
