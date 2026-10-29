// BatteryWatch - battery protocols for the Logitech PRO X SUPERLIGHT (HID++ over
// LIGHTSPEED) and the HyperX Cloud III S Wireless (USB dongle).
//
// This file is platform-neutral: it only talks to an abstract Transport, so the
// protocol logic can be unit-tested without real hardware. Every request sent
// here is a read-only query; nothing changes a setting on any device.
//
// Protocol references:
//   - Logitech HID++ 2.0: Solaar and LGSTrayBattery (voltage table).
//   - HyperX Cloud III S: auto94/HyperX-Cloud-2-Battery-Monitor and
//     dddd-zdf/gear-tray (query 0c 02 03 01 00 06, reply byte 6 = percent).
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

namespace bw {

// Results below zero mean "no reading".
constexpr int kNoDevice = -1;    // receiver/dongle not plugged in
constexpr int kNoResponse = -2;  // dongle present, device off/asleep/unreadable

// Optional diagnostic output (used by BatteryCheck.exe, off in the plugin).
using TraceFn = void (*)(const std::string&);
inline TraceFn g_trace = nullptr;

inline void trace(const std::string& msg) {
    if (g_trace) g_trace(msg);
}

inline std::string hex(const uint8_t* data, size_t len) {
    std::string out;
    char buf[4];
    for (size_t i = 0; i < len; ++i) {
        std::snprintf(buf, sizeof buf, i ? " %02x" : "%02x", data[i]);
        out += buf;
    }
    return out;
}

// One open HID collection.
class Transport {
public:
    virtual ~Transport() = default;
    // Sends one output report. data[0] is the report ID.
    virtual bool write(const uint8_t* data, size_t len) = 0;
    // Reads one input report. Returns bytes read, 0 on timeout, <0 on error.
    virtual int read(uint8_t* buf, size_t cap, int timeoutMs) = 0;
};

// ---------------------------------------------------------------------------
// Logitech HID++
// ---------------------------------------------------------------------------

using HidppMsg = std::array<uint8_t, 7>;  // short report: 10 slot idx fn|sw p0 p1 p2

enum class HidppResult { Reply, Timeout, DeviceError };

// Voltage (mV) to percent, the 100-point table used by LGSTrayBattery.
inline int voltageToPercent(int mv) {
    static const uint16_t kMv[100] = {
        4186, 4156, 4143, 4133, 4122, 4113, 4103, 4094, 4086, 4075, 4067, 4059, 4051, 4043, 4035,
        4027, 4019, 4011, 4003, 3997, 3989, 3983, 3976, 3969, 3961, 3955, 3949, 3942, 3935, 3929,
        3922, 3916, 3909, 3902, 3896, 3890, 3883, 3877, 3870, 3865, 3859, 3853, 3848, 3842, 3837,
        3833, 3828, 3824, 3819, 3815, 3811, 3808, 3804, 3800, 3797, 3793, 3790, 3787, 3784, 3781,
        3778, 3775, 3772, 3770, 3767, 3764, 3762, 3759, 3757, 3754, 3751, 3748, 3744, 3741, 3737,
        3734, 3730, 3726, 3724, 3720, 3717, 3714, 3710, 3706, 3702, 3697, 3693, 3688, 3683, 3677,
        3671, 3666, 3662, 3658, 3654, 3646, 3633, 3612, 3579, 3537,
    };
    for (int i = 0; i < 100; ++i)
        if (mv > kMv[i]) return 100 - i;
    return 0;
}

// Is this report the reply to `req`? Replies may arrive as a short (0x10, 7
// bytes) or long (0x11, 20 bytes) report, on either collection.
inline bool hidppIsReply(const HidppMsg& req, const uint8_t* r, int n) {
    const bool sized = (n >= 7 && r[0] == 0x10) || (n >= 20 && r[0] == 0x11);
    return sized && r[1] == req[1] && r[2] == req[2] && r[3] == req[3] && r[2] != 0x8f;
}

// HID++ 1.0 errors use 0x8f, HID++ 2.0 errors 0xff: [id slot err idx fn code].
inline bool hidppIsError(const HidppMsg& req, const uint8_t* r, int n) {
    return n >= 6 && (r[0] == 0x10 || r[0] == 0x11) && r[1] == req[1] &&
           (r[2] == 0x8f || r[2] == 0xff) && r[3] == req[2] && r[4] == req[3];
}

// Writes on the short collection; the reply can come back on either one.
inline HidppResult hidppQueryOnce(Transport& shortT, Transport& longT, const HidppMsg& req,
                                  HidppMsg& out, int timeoutMs) {
    if (!shortT.write(req.data(), req.size())) {
        trace("HID++ write failed: " + hex(req.data(), req.size()));
        return HidppResult::Timeout;
    }
    trace("HID++ write: " + hex(req.data(), req.size()));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    uint8_t buf[64];
    while (std::chrono::steady_clock::now() < deadline) {
        Transport* sources[2] = {&shortT, &longT};
        const int waits[2] = {&shortT == &longT ? 10 : 0, 10};
        for (int s = 0; s < 2; ++s) {
            if (s == 1 && sources[1] == sources[0]) continue;
            const int n = sources[s]->read(buf, sizeof buf, waits[s]);
            if (n < 0) return HidppResult::Timeout;
            if (n == 0) continue;
            trace("HID++ read:  " + hex(buf, (size_t)n));
            if (hidppIsError(req, buf, n)) return HidppResult::DeviceError;
            if (hidppIsReply(req, buf, n)) {
                std::memcpy(out.data(), buf, 7);
                return HidppResult::Reply;
            }
        }
    }
    return HidppResult::Timeout;
}

// Up to three attempts. Each retry uses a new software ID (low nibble of the
// function byte) so a late reply to an earlier attempt is not mistaken for it.
inline bool hidppQuery(Transport& shortT, Transport& longT, HidppMsg req, HidppMsg& out,
                       int timeoutMs = 300) {
    const uint8_t fn = req[3];
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        req[3] = (uint8_t)((fn & 0xf0) | ((fn & 0x0f) + attempt));
        switch (hidppQueryOnce(shortT, longT, req, out, timeoutMs)) {
            case HidppResult::Reply: return true;
            case HidppResult::DeviceError: return false;
            case HidppResult::Timeout: break;
        }
    }
    return false;
}

inline bool hidppPing(Transport& s, Transport& l, uint8_t slot) {
    HidppMsg out{};
    // Root feature, function 1 (protocol version); 0x55 is echoed back.
    return hidppQuery(s, l, {0x10, slot, 0x00, 0x1a, 0x00, 0x00, 0x55}, out) && out[6] == 0x55;
}

// Looks up a feature's index on the device. 0 means not supported.
inline uint8_t hidppFeatureIndex(Transport& s, Transport& l, uint8_t slot, uint16_t feature) {
    HidppMsg out{};
    if (!hidppQuery(s, l, {0x10, slot, 0x00, 0x0a, (uint8_t)(feature >> 8), (uint8_t)feature, 0},
                    out))
        return 0;
    return out[4];
}

// Reads the battery of the first mouse found behind this receiver (slots 1-6)
// or on a direct USB cable (slot 0xff).
inline int readLogitechMouse(Transport& s, Transport& l) {
    for (uint8_t slot : {1, 2, 3, 4, 5, 6, 0xff}) {
        if (!hidppPing(s, l, slot)) continue;
        trace("HID++ device answered in slot " + std::to_string(slot));

        // Skip keyboards etc. sharing the receiver (0x0005 type 3 = mouse).
        if (uint8_t nameIdx = hidppFeatureIndex(s, l, slot, 0x0005)) {
            HidppMsg out{};
            if (hidppQuery(s, l, {0x10, slot, nameIdx, 0x2a, 0, 0, 0}, out) && out[4] != 3) {
                trace("  not a mouse (device type " + std::to_string(out[4]) + "), skipping");
                continue;
            }
        }
        for (uint16_t feature : {0x1004, 0x1000, 0x1001}) {
            const uint8_t idx = hidppFeatureIndex(s, l, slot, feature);
            if (!idx) continue;
            // 0x1004 reads status with function 1; 0x1000/0x1001 with function 0.
            const uint8_t fn = feature == 0x1004 ? 0x1a : 0x0a;
            HidppMsg out{};
            if (!hidppQuery(s, l, {0x10, slot, idx, fn, 0, 0, 0}, out)) continue;
            int pct = feature == 0x1001 ? voltageToPercent((out[4] << 8) | out[5]) : out[4];
            char msg[64];
            std::snprintf(msg, sizeof msg, "  battery feature %04x -> %d%%", feature, pct);
            trace(msg);
            if (pct >= 0 && pct <= 100) return pct;
        }
    }
    return kNoResponse;
}

// ---------------------------------------------------------------------------
// HyperX Cloud III S Wireless
// ---------------------------------------------------------------------------

inline int readHyperXCloud3S(Transport& t) {
    uint8_t req[52] = {0x0c, 0x02, 0x03, 0x01, 0x00, 0x06};
    if (!t.write(req, sizeof req)) {
        trace("HyperX write failed");
        return kNoResponse;
    }
    trace("HyperX write: " + hex(req, 6) + " (padded)");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    uint8_t buf[64];
    while (std::chrono::steady_clock::now() < deadline) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - std::chrono::steady_clock::now())
                              .count();
        const int n = t.read(buf, sizeof buf, left > 1 ? (int)left : 1);
        if (n < 0) return kNoResponse;
        if (n == 0) continue;
        trace("HyperX read:  " + hex(buf, (size_t)(n < 12 ? n : 12)));
        // The reply echoes the 6-byte query; byte 6 is the percentage, and
        // 0xff means the headset is switched off.
        if (n > 6 && std::memcmp(buf, req, 6) == 0)
            return buf[6] <= 100 ? (int)buf[6] : kNoResponse;
    }
    return kNoResponse;
}

}  // namespace bw
