// BatteryCheck - console tool to test the readers before using Rainmeter.
// Lists the relevant HID collections, then reads both batteries with a full
// trace of every report sent and received. Read-only.
#include "hid_win.hpp"

#include <cstdio>

static void printTrace(const std::string& s) { std::printf("    %s\n", s.c_str()); }

static void listDevices(const char* label, uint16_t vid) {
    std::printf("%s HID collections (VID %04X):\n", label, vid);
    const auto all = bw::hidEnumerate(vid, 0);
    if (all.empty()) std::printf("  (none found)\n");
    for (const auto& d : all)
        std::printf("  PID %04X  page %04X usage %04X  in %2u out %2u  %ls\n", d.pid, d.usagePage,
                    d.usage, d.inputLen, d.outputLen, d.product.c_str());
    std::printf("\n");
}

static void report(const char* label, int v) {
    if (v >= 0)
        std::printf("==> %s battery: %d%%\n\n", label, v);
    else if (v == bw::kNoDevice)
        std::printf("==> %s: receiver/dongle not found\n\n", label);
    else
        std::printf("==> %s: dongle found, but no battery reply (device off or asleep?)\n\n", label);
}

int main() {
    listDevices("Logitech", bw::kLogitechVid);
    listDevices("HyperX", bw::kHyperXVid);

    bw::g_trace = printTrace;
    std::printf("Reading mouse (move it first so it's awake)...\n");
    report("Mouse", bw::readMouseBattery());
    std::printf("Reading headset (make sure it's switched on)...\n");
    report("Headset", bw::readHeadsetBattery(bw::kCloud3SPid));

    std::printf("Press Enter to close.");
    std::getchar();
    return 0;
}
