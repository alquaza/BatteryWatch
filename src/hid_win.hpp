// BatteryWatch - minimal Windows HID access (hid.dll + setupapi), no extra libraries.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "protocol.hpp"

namespace bw {

// One HID top-level collection. A single USB receiver shows up as several.
struct HidInfo {
    std::wstring path;
    std::wstring product;
    uint16_t vid = 0, pid = 0;
    uint16_t usagePage = 0, usage = 0;
    uint16_t inputLen = 0, outputLen = 0;
};

// vid/pid of 0 match anything.
std::vector<HidInfo> hidEnumerate(uint16_t vid, uint16_t pid);

class HidDevice : public Transport {
public:
    HidDevice() = default;
    ~HidDevice() override;
    HidDevice(const HidDevice&) = delete;
    HidDevice& operator=(const HidDevice&) = delete;

    bool open(const HidInfo& info);
    void close();
    bool isOpen() const { return handle_ != INVALID_HANDLE_VALUE; }

    bool write(const uint8_t* data, size_t len) override;
    int read(uint8_t* buf, size_t cap, int timeoutMs) override;

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE readEvent_ = nullptr;
    HANDLE writeEvent_ = nullptr;
    uint16_t inputLen_ = 0, outputLen_ = 0;
};

// High-level readers. Return 0-100, kNoDevice or kNoResponse.
int readMouseBattery();
int readHeadsetBattery(uint16_t headsetPid);

constexpr uint16_t kLogitechVid = 0x046d;
constexpr uint16_t kHyperXVid = 0x03f0;
constexpr uint16_t kCloud3SPid = 0x06be;

}  // namespace bw
