// BatteryWatch - Windows HID implementation and device lookup.
#include "hid_win.hpp"

#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <algorithm>
#include <cwctype>

namespace bw {

std::vector<HidInfo> hidEnumerate(uint16_t vid, uint16_t pid) {
    std::vector<HidInfo> result;
    GUID guid;
    HidD_GetHidGuid(&guid);
    HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return result;

    SP_DEVICE_INTERFACE_DATA ifd{};
    ifd.cbSize = sizeof ifd;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &guid, i, &ifd); ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &needed, nullptr);
        if (!needed) continue;
        std::vector<BYTE> buf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, needed, nullptr, nullptr)) continue;

        // Access 0 = query only. Works even for collections Windows owns
        // exclusively (the normal mouse/keyboard input).
        HANDLE h = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES attr{};
        attr.Size = sizeof attr;
        if (HidD_GetAttributes(h, &attr) && (!vid || attr.VendorID == vid) &&
            (!pid || attr.ProductID == pid)) {
            HidInfo info;
            info.path = detail->DevicePath;
            info.vid = attr.VendorID;
            info.pid = attr.ProductID;
            PHIDP_PREPARSED_DATA pp = nullptr;
            if (HidD_GetPreparsedData(h, &pp)) {
                HIDP_CAPS caps{};
                if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                    info.usagePage = caps.UsagePage;
                    info.usage = caps.Usage;
                    info.inputLen = caps.InputReportByteLength;
                    info.outputLen = caps.OutputReportByteLength;
                }
                HidD_FreePreparsedData(pp);
            }
            wchar_t name[128] = {};
            if (HidD_GetProductString(h, name, sizeof name)) info.product = name;
            result.push_back(std::move(info));
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

HidDevice::~HidDevice() { close(); }

bool HidDevice::open(const HidInfo& info) {
    close();
    handle_ = CreateFileW(info.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                          FILE_FLAG_OVERLAPPED, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) return false;
    readEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    writeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    inputLen_ = info.inputLen;
    outputLen_ = info.outputLen;
    if (!readEvent_ || !writeEvent_) {
        close();
        return false;
    }
    return true;
}

void HidDevice::close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CancelIo(handle_);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    if (readEvent_) CloseHandle(readEvent_), readEvent_ = nullptr;
    if (writeEvent_) CloseHandle(writeEvent_), writeEvent_ = nullptr;
}

bool HidDevice::write(const uint8_t* data, size_t len) {
    if (!isOpen() || outputLen_ == 0) return false;
    // Windows wants the full report length; pad with zeros.
    std::vector<uint8_t> buf(std::max<size_t>(len, outputLen_), 0);
    std::copy(data, data + len, buf.begin());

    OVERLAPPED ov{};
    ov.hEvent = writeEvent_;
    ResetEvent(writeEvent_);
    if (!WriteFile(handle_, buf.data(), (DWORD)buf.size(), nullptr, &ov) &&
        GetLastError() != ERROR_IO_PENDING)
        return false;
    DWORD n = 0;
    if (WaitForSingleObject(writeEvent_, 1000) != WAIT_OBJECT_0) {
        CancelIo(handle_);
        GetOverlappedResult(handle_, &ov, &n, TRUE);
        return false;
    }
    return GetOverlappedResult(handle_, &ov, &n, FALSE) && n > 0;
}

int HidDevice::read(uint8_t* out, size_t cap, int timeoutMs) {
    if (!isOpen() || inputLen_ == 0) return -1;
    std::vector<uint8_t> buf(inputLen_);
    OVERLAPPED ov{};
    ov.hEvent = readEvent_;
    ResetEvent(readEvent_);
    DWORD n = 0;
    if (!ReadFile(handle_, buf.data(), (DWORD)buf.size(), nullptr, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) return -1;
        if (WaitForSingleObject(readEvent_, (DWORD)timeoutMs) != WAIT_OBJECT_0) {
            CancelIo(handle_);
            // The read may have completed just before the cancel landed.
            if (!GetOverlappedResult(handle_, &ov, &n, TRUE) || n == 0) return 0;
        }
    }
    if (n == 0 && !GetOverlappedResult(handle_, &ov, &n, FALSE)) return -1;
    const size_t copy = std::min<size_t>(n, cap);
    std::copy(buf.begin(), buf.begin() + copy, out);
    return (int)copy;
}

// Two collections belong to the same receiver interface when their paths match
// apart from the "&colNN" part, e.g.
//   \\?\hid#vid_046d&pid_c547&mi_02&col01#8&1a2b3c&0&0000#{guid}
//   \\?\hid#vid_046d&pid_c547&mi_02&col02#8&1a2b3c&0&0001#{guid}
static bool sameContainer(const std::wstring& a, const std::wstring& b) {
    auto key = [](std::wstring p) -> std::wstring {
        std::transform(p.begin(), p.end(), p.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
        const size_t h1 = p.find(L'#');
        if (h1 == std::wstring::npos) return {};
        const size_t h2 = p.find(L'#', h1 + 1);
        if (h2 == std::wstring::npos) return {};
        const size_t h3 = p.find(L'#', h2 + 1);
        std::wstring model = p.substr(h1 + 1, h2 - h1 - 1);
        std::wstring inst = p.substr(h2 + 1, (h3 == std::wstring::npos ? p.size() : h3) - h2 - 1);
        const size_t col = model.find(L"&col");
        if (col != std::wstring::npos) model.resize(col);
        const size_t amp = inst.rfind(L'&');
        if (amp != std::wstring::npos) inst.resize(amp);
        return model + L"|" + inst;
    };
    const std::wstring ka = key(a);
    return !ka.empty() && ka == key(b);
}

int readMouseBattery() {
    const auto all = hidEnumerate(kLogitechVid, 0);
    int result = kNoDevice;
    for (const auto& s : all) {
        // HID++ short reports live on vendor page 0xFF00, usage 1.
        if ((s.usagePage & 0xff00) != 0xff00 || s.usage != 0x0001) continue;
        HidDevice shortDev, longDev;
        if (!shortDev.open(s)) continue;
        result = kNoResponse;
        Transport* longT = &shortDev;
        for (const auto& l : all) {
            if ((l.usagePage & 0xff00) == 0xff00 && l.usage == 0x0002 && sameContainer(l.path, s.path) &&
                longDev.open(l)) {
                longT = &longDev;
                break;
            }
        }
        const int pct = readLogitechMouse(shortDev, *longT);
        if (pct >= 0) return pct;
    }
    return result;
}

int readHeadsetBattery(uint16_t headsetPid) {
    int result = kNoDevice;
    for (const auto& info : hidEnumerate(kHyperXVid, headsetPid)) {
        // The Cloud III S dongle answers battery queries on usage page 0x01C0, usage 1.
        if (info.usagePage != 0x01c0 || info.usage != 0x0001) continue;
        HidDevice dev;
        if (!dev.open(info)) continue;
        result = kNoResponse;
        const int pct = readHyperXCloud3S(dev);
        if (pct >= 0) return pct;
    }
    return result;
}

}  // namespace bw
