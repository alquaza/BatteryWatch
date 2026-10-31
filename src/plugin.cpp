// BatteryWatch - Rainmeter plugin.
//
// Skin usage:
//   [MeasureMouse]
//   Measure=Plugin
//   Plugin=BatteryWatch
//   Device=Mouse            ; or Headset
//   PollSeconds=60          ; optional, minimum 10
//   HeadsetPID=0x06BE       ; optional, only for Device=Headset
//
// Number value: battery percent, or -1 if there is no current reading.
// String value: "64%" or "--".
//
// Device reads happen on one background thread per device type, shared by every
// measure (and skin) that uses it, so Rainmeter itself never waits on USB. A
// failed read clears the current value, so unavailable devices show "--" and
// do not trigger a low-battery warning.
#include "hid_win.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <thread>

#define PLUGIN_EXPORT extern "C" __declspec(dllexport)

namespace {

// Rainmeter API, resolved at runtime so no import library is needed to build.
using RmReadStringFn = LPCWSTR(__stdcall*)(void*, LPCWSTR, LPCWSTR, BOOL);
using RmReadFormulaFn = double(__stdcall*)(void*, LPCWSTR, double);

LPCWSTR readString(void* rm, LPCWSTR option, LPCWSTR def) {
    static auto fn = (RmReadStringFn)(void*)GetProcAddress(GetModuleHandleW(L"Rainmeter.dll"), "RmReadString");
    return fn ? fn(rm, option, def, TRUE) : def;
}

double readNumber(void* rm, LPCWSTR option, double def) {
    static auto fn = (RmReadFormulaFn)(void*)GetProcAddress(GetModuleHandleW(L"Rainmeter.dll"), "RmReadFormula");
    return fn ? fn(rm, option, def) : def;
}

enum class Kind { Mouse = 0, Headset = 1 };

class Poller {
public:
    explicit Poller(Kind kind) : kind_(kind) {}

    void attach() {
        std::lock_guard<std::mutex> g(lifecycle_);
        if (refs_++ == 0) {
            stop_ = false;
            thread_ = std::thread([this] { run(); });
        }
    }

    void detach() {
        std::lock_guard<std::mutex> g(lifecycle_);
        if (refs_ == 0 || --refs_ > 0) return;
        {
            std::lock_guard<std::mutex> w(waitMutex_);
            stop_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    void configure(int pollSeconds, uint16_t headsetPid) {
        interval_ = pollSeconds < 10 ? 10 : pollSeconds;
        if (headsetPid) headsetPid_ = headsetPid;
    }

    int last() const { return last_; }

private:
    void run() {
        for (;;) {
            const int v = kind_ == Kind::Mouse ? bw::readMouseBattery()
                                               : bw::readHeadsetBattery(headsetPid_);
            last_ = v;
            std::unique_lock<std::mutex> lk(waitMutex_);
            if (wake_.wait_for(lk, std::chrono::seconds(interval_.load()), [this] { return stop_; }))
                return;
        }
    }

    const Kind kind_;
    std::mutex lifecycle_;
    int refs_ = 0;
    std::thread thread_;
    std::mutex waitMutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::atomic<int> interval_{60};
    std::atomic<uint16_t> headsetPid_{bw::kCloud3SPid};
    std::atomic<int> last_{-1};
};

Poller& poller(Kind k) {
    static Poller mouse(Kind::Mouse), headset(Kind::Headset);
    return k == Kind::Mouse ? mouse : headset;
}

struct Measure {
    Kind kind = Kind::Mouse;
    bool attached = false;
    wchar_t text[16] = L"--";
};

}  // namespace

PLUGIN_EXPORT void Initialize(void** data, void* /*rm*/) { *data = new Measure; }

PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue) {
    auto* m = static_cast<Measure*>(data);
    *maxValue = 100;

    const Kind kind = _wcsicmp(readString(rm, L"Device", L"Mouse"), L"Headset") == 0 ? Kind::Headset : Kind::Mouse;
    if (m->attached && kind != m->kind) {
        poller(m->kind).detach();
        m->attached = false;
    }
    m->kind = kind;

    const uint16_t pid = (uint16_t)wcstoul(readString(rm, L"HeadsetPID", L"0x06BE"), nullptr, 0);
    poller(kind).configure((int)readNumber(rm, L"PollSeconds", 60), pid);
    if (!m->attached) {
        poller(kind).attach();
        m->attached = true;
    }
}

PLUGIN_EXPORT double Update(void* data) {
    auto* m = static_cast<Measure*>(data);
    return poller(m->kind).last();
}

PLUGIN_EXPORT LPCWSTR GetString(void* data) {
    auto* m = static_cast<Measure*>(data);
    const int v = poller(m->kind).last();
    if (v >= 0)
        swprintf(m->text, 16, L"%d%%", v);
    else
        wcscpy(m->text, L"--");
    return m->text;
}

PLUGIN_EXPORT void Finalize(void* data) {
    auto* m = static_cast<Measure*>(data);
    if (m->attached) poller(m->kind).detach();
    delete m;
}
