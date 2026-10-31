// Métricas de CPU, memória e bateria (sem dependência de Qt).
// Valores indisponíveis são NaN.
#pragma once

#include <cmath>
#include <limits>
#include <cstdint>
#include <string>
#include <vector>

namespace linosd {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
inline bool has(double v) { return !std::isnan(v); }

struct CpuSample {
    double usage = kNaN;        // %
    double maxCoreUsage = kNaN; // % do núcleo mais ocupado
    double temp = kNaN;         // °C
    double freqMHz = kNaN;      // média
    double maxFreqMHz = kNaN;   // núcleo mais rápido
    double powerW = kNaN;
    std::vector<double> coreUsage;
};

struct MemSample {
    double usedMiB = kNaN;
    double totalMiB = kNaN;
    double swapUsedMiB = kNaN;
    double swapTotalMiB = kNaN;
};

struct BatterySample {
    double percent = kNaN;
    double powerW = kNaN;
    bool charging = false;
};

class CpuMonitor {
public:
    CpuMonitor();
    CpuSample sample();
    const std::string& name() const { return name_; }
    const std::string& tempSource() const { return tempPath_; }

private:
    struct Times {
        uint64_t idle = 0, total = 0;
    };
    std::vector<Times> prev_;  // [0] = total, [1..] = núcleos
    std::string name_;
    std::string tempPath_;
    std::string raplPath_;
    std::string hwmonPowerPath_;  // zenpower etc.
    uint64_t prevEnergyUj_ = 0;
    uint64_t prevEnergyTimeNs_ = 0;
    uint64_t raplMaxUj_ = 0;

    void detectTemp();
    void detectPower();
};

class MemMonitor {
public:
    MemSample sample();
};

class BatteryMonitor {
public:
    BatteryMonitor();
    BatterySample sample();
    bool present() const { return !path_.empty(); }

private:
    std::string path_;
};

uint64_t monotonicNs();

}  // namespace linosd
