#include "system_monitor.h"

#include <time.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

#include "sysfs.h"

namespace linosd {

uint64_t monotonicNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

// ---------------------------------------------------------------------------
// CPU
// ---------------------------------------------------------------------------
CpuMonitor::CpuMonitor() {
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    while (std::getline(f, line)) {
        if (sysfs::startsWith(line, "model name")) {
            auto p = line.find(':');
            if (p != std::string::npos) name_ = sysfs::trim(line.substr(p + 1));
            break;
        }
    }
    detectTemp();
    detectPower();
    sample();  // inicializa os contadores
}

void CpuMonitor::detectTemp() {
    const std::string base = "/sys/class/hwmon/";
    // Ordem de preferência por driver e rótulo do sensor.
    struct Pref {
        const char* driver;
        const char* label;  // nullptr = temp1_input
    };
    const Pref prefs[] = {
        {"k10temp", "Tctl"}, {"k10temp", "Tdie"}, {"zenpower", "Tdie"}, {"zenpower", "Tctl"},
        {"coretemp", "Package id 0"}, {"k10temp", nullptr}, {"zenpower", nullptr}, {"coretemp", nullptr},
        {"cpu_thermal", nullptr}, {"soc_thermal", nullptr}, {"acpitz", nullptr},
    };
    auto dirs = sysfs::listDir(base);
    for (const Pref& p : prefs) {
        for (const auto& d : dirs) {
            std::string dir = base + d + "/";
            auto name = sysfs::readText(dir + "name");
            if (!name || *name != p.driver) continue;
            if (!p.label) {
                if (sysfs::exists(dir + "temp1_input")) {
                    tempPath_ = dir + "temp1_input";
                    return;
                }
                continue;
            }
            for (int i = 1; i < 64; ++i) {
                auto lbl = sysfs::readText(dir + "temp" + std::to_string(i) + "_label");
                if (lbl && *lbl == p.label) {
                    tempPath_ = dir + "temp" + std::to_string(i) + "_input";
                    return;
                }
            }
        }
    }
    // Fallback: zona térmica x86_pkg_temp
    for (const auto& z : sysfs::listDir("/sys/class/thermal/")) {
        if (!sysfs::startsWith(z, "thermal_zone")) continue;
        auto type = sysfs::readText("/sys/class/thermal/" + z + "/type");
        if (type && (*type == "x86_pkg_temp" || *type == "cpu-thermal")) {
            tempPath_ = "/sys/class/thermal/" + z + "/temp";
            return;
        }
    }
}

void CpuMonitor::detectPower() {
    // RAPL (Intel e AMD Zen). Em kernels recentes energy_uj só é legível pelo root;
    // o install.sh oferece uma regra udev opcional para liberar a leitura.
    for (const auto& d : sysfs::listDir("/sys/class/powercap/")) {
        std::string dir = "/sys/class/powercap/" + d + "/";
        auto name = sysfs::readText(dir + "name");
        if (name && sysfs::startsWith(*name, "package") && sysfs::readInt(dir + "energy_uj")) {
            raplPath_ = dir + "energy_uj";
            auto mx = sysfs::readInt(dir + "max_energy_range_uj");
            raplMaxUj_ = mx ? static_cast<uint64_t>(*mx) : 0;
            return;
        }
    }
    // zenpower expõe power1_input (µW)
    for (const auto& d : sysfs::listDir("/sys/class/hwmon/")) {
        std::string dir = "/sys/class/hwmon/" + d + "/";
        auto name = sysfs::readText(dir + "name");
        if (name && *name == "zenpower" && sysfs::exists(dir + "power1_input")) {
            hwmonPowerPath_ = dir + "power1_input";
            return;
        }
    }
}

CpuSample CpuMonitor::sample() {
    CpuSample s;
    std::ifstream f("/proc/stat");
    std::string line;
    std::vector<Times> cur;
    while (std::getline(f, line)) {
        if (!sysfs::startsWith(line, "cpu")) break;
        std::istringstream is(line);
        std::string label;
        uint64_t v[10] = {0};
        is >> label;
        for (auto& x : v) is >> x;
        // user nice system idle iowait irq softirq steal guest guest_nice
        Times t;
        t.idle = v[3] + v[4];
        t.total = v[0] + v[1] + v[2] + v[3] + v[4] + v[5] + v[6] + v[7];
        cur.push_back(t);
    }
    if (prev_.size() == cur.size() && !cur.empty()) {
        auto pct = [](const Times& a, const Times& b) {
            uint64_t dt = b.total - a.total, di = b.idle - a.idle;
            if (dt == 0) return 0.0;
            return std::clamp(100.0 * static_cast<double>(dt - di) / static_cast<double>(dt), 0.0, 100.0);
        };
        s.usage = pct(prev_[0], cur[0]);
        double mx = 0;
        for (size_t i = 1; i < cur.size(); ++i) {
            double u = pct(prev_[i], cur[i]);
            s.coreUsage.push_back(u);
            mx = std::max(mx, u);
        }
        s.maxCoreUsage = mx;
    }
    prev_ = cur;

    if (!tempPath_.empty()) {
        if (auto t = sysfs::readInt(tempPath_)) s.temp = static_cast<double>(*t) / 1000.0;
    }

    // Frequência: scaling_cur_freq (kHz) de cada CPU.
    double sum = 0, mx = 0;
    int n = 0;
    for (int i = 0; i < 1024; ++i) {
        auto v = sysfs::readInt("/sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/scaling_cur_freq");
        if (!v) {
            if (!sysfs::exists("/sys/devices/system/cpu/cpu" + std::to_string(i))) break;
            continue;
        }
        double mhz = static_cast<double>(*v) / 1000.0;
        sum += mhz;
        mx = std::max(mx, mhz);
        ++n;
    }
    if (n > 0) {
        s.freqMHz = sum / n;
        s.maxFreqMHz = mx;
    } else {
        // Fallback (VMs): "cpu MHz" do /proc/cpuinfo
        std::ifstream ci("/proc/cpuinfo");
        while (std::getline(ci, line)) {
            if (sysfs::startsWith(line, "cpu MHz")) {
                auto p = line.find(':');
                double mhz = std::atof(line.c_str() + p + 1);
                sum += mhz;
                mx = std::max(mx, mhz);
                ++n;
            }
        }
        if (n > 0) {
            s.freqMHz = sum / n;
            s.maxFreqMHz = mx;
        }
    }

    // Potência
    if (!raplPath_.empty()) {
        if (auto e = sysfs::readInt(raplPath_)) {
            uint64_t now = monotonicNs();
            uint64_t energy = static_cast<uint64_t>(*e);
            if (prevEnergyTimeNs_ != 0 && now > prevEnergyTimeNs_) {
                uint64_t de = energy >= prevEnergyUj_ ? energy - prevEnergyUj_ : (raplMaxUj_ - prevEnergyUj_) + energy;
                double dt = static_cast<double>(now - prevEnergyTimeNs_) / 1e9;
                if (dt > 0.05) s.powerW = static_cast<double>(de) / 1e6 / dt;
            }
            prevEnergyUj_ = energy;
            prevEnergyTimeNs_ = now;
        }
    } else if (!hwmonPowerPath_.empty()) {
        if (auto p = sysfs::readInt(hwmonPowerPath_)) s.powerW = static_cast<double>(*p) / 1e6;
    }
    return s;
}

// ---------------------------------------------------------------------------
// Memória
// ---------------------------------------------------------------------------
MemSample MemMonitor::sample() {
    MemSample s;
    std::ifstream f("/proc/meminfo");
    std::string key;
    uint64_t value;
    std::string unit;
    uint64_t total = 0, avail = 0, swapTotal = 0, swapFree = 0;
    bool haveAvail = false;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream is(line);
        is >> key >> value >> unit;
        if (key == "MemTotal:") total = value;
        else if (key == "MemAvailable:") {
            avail = value;
            haveAvail = true;
        } else if (key == "SwapTotal:") swapTotal = value;
        else if (key == "SwapFree:") swapFree = value;
    }
    if (total > 0 && haveAvail) {
        s.totalMiB = static_cast<double>(total) / 1024.0;
        s.usedMiB = static_cast<double>(total - avail) / 1024.0;
    }
    s.swapTotalMiB = static_cast<double>(swapTotal) / 1024.0;
    s.swapUsedMiB = static_cast<double>(swapTotal - swapFree) / 1024.0;
    return s;
}

// ---------------------------------------------------------------------------
// Bateria
// ---------------------------------------------------------------------------
BatteryMonitor::BatteryMonitor() {
    for (const auto& d : sysfs::listDir("/sys/class/power_supply/")) {
        std::string dir = "/sys/class/power_supply/" + d + "/";
        auto type = sysfs::readText(dir + "type");
        if (type && *type == "Battery" && sysfs::exists(dir + "capacity")) {
            path_ = dir;
            return;
        }
    }
}

BatterySample BatteryMonitor::sample() {
    BatterySample s;
    if (path_.empty()) return s;
    if (auto c = sysfs::readInt(path_ + "capacity")) s.percent = static_cast<double>(*c);
    if (auto st = sysfs::readText(path_ + "status")) s.charging = (*st == "Charging" || *st == "Full");
    if (auto p = sysfs::readInt(path_ + "power_now")) {
        s.powerW = static_cast<double>(*p) / 1e6;
    } else {
        auto i = sysfs::readInt(path_ + "current_now");
        auto v = sysfs::readInt(path_ + "voltage_now");
        if (i && v) s.powerW = static_cast<double>(*i) * static_cast<double>(*v) / 1e12;
    }
    return s;
}

}  // namespace linosd
