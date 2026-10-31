// Monitor de GPU com backends: AMD (amdgpu sysfs), NVIDIA (NVML via dlopen)
// e Intel (i915/xe sysfs).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "system_monitor.h"

namespace linosd {

struct GpuSample {
    double usage = kNaN;       // %
    double temp = kNaN;        // °C (borda)
    double hotspot = kNaN;     // °C (junção)
    double coreMHz = kNaN;
    double memMHz = kNaN;
    double powerW = kNaN;
    double vramUsedMiB = kNaN;
    double vramTotalMiB = kNaN;
    double fanPercent = kNaN;
    double fanRpm = kNaN;
};

class GpuBackend {
public:
    virtual ~GpuBackend() = default;
    virtual std::string name() const = 0;
    virtual std::string vendor() const = 0;
    virtual GpuSample sample() = 0;
    virtual bool discrete() const { return true; }
};

struct GpuInfo {
    std::string id;    // identificador estável (ex.: "pci:0000:03:00.0")
    std::string name;  // nome amigável
    std::string vendor;
};

class GpuMonitor {
public:
    GpuMonitor();
    ~GpuMonitor();

    std::vector<GpuInfo> list() const;
    // id vazio = automático (prefere GPU dedicada).
    void select(const std::string& id);
    GpuSample sample();
    std::string currentName() const;

private:
    struct Entry {
        GpuInfo info;
        std::unique_ptr<GpuBackend> backend;
    };
    std::vector<Entry> gpus_;
    int current_ = -1;
};

}  // namespace linosd
