#include "gpu_monitor.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "sysfs.h"

namespace linosd {
namespace {

constexpr uint32_t kVendorAmd = 0x1002;
constexpr uint32_t kVendorNvidia = 0x10de;
constexpr uint32_t kVendorIntel = 0x8086;

std::string findHwmon(const std::string& deviceDir) {
    auto entries = sysfs::listDir(deviceDir + "hwmon/");
    if (entries.empty()) return {};
    return deviceDir + "hwmon/" + entries.front() + "/";
}

// Lê a linha marcada com '*' em pp_dpm_sclk / pp_dpm_mclk ("1: 1800Mhz *").
double readDpmActive(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.find('*') == std::string::npos) continue;
        auto c = line.find(':');
        if (c == std::string::npos) continue;
        return std::atof(line.c_str() + c + 1);
    }
    return kNaN;
}

// ---------------------------------------------------------------------------
// AMD (amdgpu)
// ---------------------------------------------------------------------------
class AmdBackend : public GpuBackend {
public:
    AmdBackend(std::string dev, std::string name) : dev_(std::move(dev)), name_(std::move(name)) {
        hwmon_ = findHwmon(dev_);
        // Identifica temperaturas pelo rótulo (edge / junction).
        for (int i = 1; i <= 3 && !hwmon_.empty(); ++i) {
            auto lbl = sysfs::readText(hwmon_ + "temp" + std::to_string(i) + "_label");
            std::string in = hwmon_ + "temp" + std::to_string(i) + "_input";
            if (!lbl) continue;
            if (*lbl == "edge") edge_ = in;
            else if (*lbl == "junction") junction_ = in;
        }
        if (edge_.empty() && !hwmon_.empty() && sysfs::exists(hwmon_ + "temp1_input")) edge_ = hwmon_ + "temp1_input";
        // APUs costumam ter pouca "VRAM" reservada; consideramos integrada se < 1 GiB.
        auto total = sysfs::readInt(dev_ + "mem_info_vram_total");
        discrete_ = total && *total >= (1ll << 30);
    }
    std::string name() const override { return name_; }
    std::string vendor() const override { return "AMD"; }
    bool discrete() const override { return discrete_; }

    GpuSample sample() override {
        GpuSample s;
        if (auto v = sysfs::readInt(dev_ + "gpu_busy_percent")) s.usage = static_cast<double>(*v);
        if (auto v = sysfs::readInt(dev_ + "mem_info_vram_used")) s.vramUsedMiB = static_cast<double>(*v) / 1048576.0;
        if (auto v = sysfs::readInt(dev_ + "mem_info_vram_total")) s.vramTotalMiB = static_cast<double>(*v) / 1048576.0;
        if (!edge_.empty())
            if (auto v = sysfs::readInt(edge_)) s.temp = static_cast<double>(*v) / 1000.0;
        if (!junction_.empty())
            if (auto v = sysfs::readInt(junction_)) s.hotspot = static_cast<double>(*v) / 1000.0;
        if (!hwmon_.empty()) {
            // power1_average (antigo) ou power1_input (RDNA3+), em µW
            if (auto v = sysfs::readInt(hwmon_ + "power1_average")) s.powerW = static_cast<double>(*v) / 1e6;
            else if (auto v2 = sysfs::readInt(hwmon_ + "power1_input")) s.powerW = static_cast<double>(*v2) / 1e6;
            if (auto v = sysfs::readInt(hwmon_ + "freq1_input")) s.coreMHz = static_cast<double>(*v) / 1e6;
            if (auto v = sysfs::readInt(hwmon_ + "freq2_input")) s.memMHz = static_cast<double>(*v) / 1e6;
            if (auto v = sysfs::readInt(hwmon_ + "fan1_input")) s.fanRpm = static_cast<double>(*v);
            auto pwm = sysfs::readInt(hwmon_ + "pwm1");
            auto pwmMax = sysfs::readInt(hwmon_ + "pwm1_max");
            if (pwm) s.fanPercent = 100.0 * static_cast<double>(*pwm) / static_cast<double>(pwmMax ? *pwmMax : 255);
        }
        if (!has(s.coreMHz)) s.coreMHz = readDpmActive(dev_ + "pp_dpm_sclk");
        if (!has(s.memMHz)) s.memMHz = readDpmActive(dev_ + "pp_dpm_mclk");
        return s;
    }

private:
    std::string dev_, name_, hwmon_, edge_, junction_;
    bool discrete_ = true;
};

// ---------------------------------------------------------------------------
// Intel (i915 / xe)
// ---------------------------------------------------------------------------
class IntelBackend : public GpuBackend {
public:
    IntelBackend(std::string card, std::string dev, std::string name)
        : card_(std::move(card)), dev_(std::move(dev)), name_(std::move(name)) {
        hwmon_ = findHwmon(dev_);
        const std::string candidates[] = {
            card_ + "gt_act_freq_mhz", card_ + "gt_cur_freq_mhz", card_ + "gt/gt0/rps_act_freq_mhz",
            dev_ + "tile0/gt0/freq0/act_freq", dev_ + "tile0/gt0/freq0/cur_freq",
        };
        for (const auto& c : candidates)
            if (sysfs::exists(c)) {
                freq_ = c;
                break;
            }
        discrete_ = !hwmon_.empty();  // Arc dedicadas expõem hwmon
    }
    std::string name() const override { return name_; }
    std::string vendor() const override { return "Intel"; }
    bool discrete() const override { return discrete_; }

    GpuSample sample() override {
        GpuSample s;
        if (!freq_.empty())
            if (auto v = sysfs::readInt(freq_)) s.coreMHz = static_cast<double>(*v);
        if (!hwmon_.empty()) {
            if (auto v = sysfs::readInt(hwmon_ + "temp1_input")) s.temp = static_cast<double>(*v) / 1000.0;
            if (auto v = sysfs::readInt(hwmon_ + "fan1_input")) s.fanRpm = static_cast<double>(*v);
            // energy1_input (µJ) -> potência
            if (auto e = sysfs::readInt(hwmon_ + "energy1_input")) {
                uint64_t now = monotonicNs();
                if (prevT_ && now > prevT_ && static_cast<uint64_t>(*e) >= prevE_)
                    s.powerW = static_cast<double>(static_cast<uint64_t>(*e) - prevE_) / 1e6 /
                               (static_cast<double>(now - prevT_) / 1e9);
                prevE_ = static_cast<uint64_t>(*e);
                prevT_ = now;
            }
        }
        // Uso da GPU Intel requer perf (i915 PMU), indisponível sem privilégios.
        return s;
    }

private:
    std::string card_, dev_, name_, hwmon_, freq_;
    bool discrete_ = false;
    uint64_t prevE_ = 0, prevT_ = 0;
};

// ---------------------------------------------------------------------------
// NVIDIA (NVML carregada dinamicamente - sem dependência em tempo de build)
// ---------------------------------------------------------------------------
using nvmlDevice_t = void*;
struct nvmlUtilization_t {
    unsigned int gpu, memory;
};
struct nvmlMemory_t {
    unsigned long long total, free, used;
};
struct nvmlPciInfo_t {
    char busIdLegacy[16];
    unsigned int domain, bus, device, pciDeviceId, pciSubSystemId;
    char busId[32];
};

class Nvml {
public:
    static Nvml& get() {
        static Nvml n;
        return n;
    }
    bool ok() const { return ok_; }
    int (*init)() = nullptr;
    int (*getCount)(unsigned int*) = nullptr;
    int (*getHandle)(unsigned int, nvmlDevice_t*) = nullptr;
    int (*getName)(nvmlDevice_t, char*, unsigned int) = nullptr;
    int (*getUtil)(nvmlDevice_t, nvmlUtilization_t*) = nullptr;
    int (*getMem)(nvmlDevice_t, nvmlMemory_t*) = nullptr;
    int (*getTemp)(nvmlDevice_t, int, unsigned int*) = nullptr;
    int (*getPower)(nvmlDevice_t, unsigned int*) = nullptr;
    int (*getClock)(nvmlDevice_t, int, unsigned int*) = nullptr;
    int (*getFan)(nvmlDevice_t, unsigned int*) = nullptr;
    int (*getPci)(nvmlDevice_t, nvmlPciInfo_t*) = nullptr;

private:
    Nvml() {
        void* h = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
        if (!h) h = dlopen("libnvidia-ml.so", RTLD_LAZY);
        if (!h) return;
        auto load = [h](auto& fn, const char* n) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(h, n)); };
        load(init, "nvmlInit_v2");
        load(getCount, "nvmlDeviceGetCount_v2");
        load(getHandle, "nvmlDeviceGetHandleByIndex_v2");
        load(getName, "nvmlDeviceGetName");
        load(getUtil, "nvmlDeviceGetUtilizationRates");
        load(getMem, "nvmlDeviceGetMemoryInfo");
        load(getTemp, "nvmlDeviceGetTemperature");
        load(getPower, "nvmlDeviceGetPowerUsage");
        load(getClock, "nvmlDeviceGetClockInfo");
        load(getFan, "nvmlDeviceGetFanSpeed");
        load(getPci, "nvmlDeviceGetPciInfo_v3");
        if (!getPci) load(getPci, "nvmlDeviceGetPciInfo_v2");
        ok_ = init && getCount && getHandle && init() == 0;
    }
    bool ok_ = false;
};

class NvidiaBackend : public GpuBackend {
public:
    NvidiaBackend(nvmlDevice_t dev, std::string name) : dev_(dev), name_(std::move(name)) {}
    std::string name() const override { return name_; }
    std::string vendor() const override { return "NVIDIA"; }

    GpuSample sample() override {
        Nvml& n = Nvml::get();
        GpuSample s;
        nvmlUtilization_t u;
        if (n.getUtil && n.getUtil(dev_, &u) == 0) s.usage = u.gpu;
        nvmlMemory_t m;
        if (n.getMem && n.getMem(dev_, &m) == 0) {
            s.vramUsedMiB = static_cast<double>(m.used) / 1048576.0;
            s.vramTotalMiB = static_cast<double>(m.total) / 1048576.0;
        }
        unsigned int v;
        if (n.getTemp && n.getTemp(dev_, 0 /*NVML_TEMPERATURE_GPU*/, &v) == 0) s.temp = v;
        if (n.getPower && n.getPower(dev_, &v) == 0) s.powerW = v / 1000.0;
        if (n.getClock && n.getClock(dev_, 0 /*GRAPHICS*/, &v) == 0) s.coreMHz = v;
        if (n.getClock && n.getClock(dev_, 2 /*MEM*/, &v) == 0) s.memMHz = v;
        if (n.getFan && n.getFan(dev_, &v) == 0) s.fanPercent = v;
        return s;
    }

private:
    nvmlDevice_t dev_;
    std::string name_;
};

std::string hexField(const std::string& path, uint32_t& out) {
    auto t = sysfs::readText(path);
    if (!t) return {};
    out = static_cast<uint32_t>(std::strtoul(t->c_str(), nullptr, 16));
    return *t;
}

}  // namespace

GpuMonitor::GpuMonitor() {
    bool nvidiaAdded = false;
    for (const auto& card : sysfs::listDir("/sys/class/drm/")) {
        // Somente "cardN" (ignora "card0-DP-1" etc.)
        if (!sysfs::startsWith(card, "card") || card.find('-') != std::string::npos) continue;
        std::string cardDir = "/sys/class/drm/" + card + "/";
        std::string dev = cardDir + "device/";
        uint32_t vendor = 0, device = 0;
        if (hexField(dev + "vendor", vendor).empty()) continue;
        hexField(dev + "device", device);

        // Endereço PCI a partir do link simbólico do dispositivo
        std::string pciAddr;
        if (auto ue = sysfs::readText(dev + "uevent")) {
            std::istringstream is(*ue);
            std::string line;
            while (std::getline(is, line))
                if (sysfs::startsWith(line, "PCI_SLOT_NAME=")) pciAddr = line.substr(14);
        }
        std::string name = sysfs::pciName(vendor, device);
        char fallback[64];

        Entry e;
        e.info.id = pciAddr.empty() ? "drm:" + card : "pci:" + pciAddr;
        if (vendor == kVendorAmd && sysfs::exists(dev + "gpu_busy_percent")) {
            if (name.empty()) {
                std::snprintf(fallback, sizeof(fallback), "AMD GPU [%04x]", device);
                name = fallback;
            }
            e.backend = std::make_unique<AmdBackend>(dev, name);
        } else if (vendor == kVendorIntel) {
            if (name.empty()) name = "Intel GPU";
            e.backend = std::make_unique<IntelBackend>(cardDir, dev, name);
        } else if (vendor == kVendorNvidia) {
            continue;  // tratado via NVML abaixo
        } else {
            continue;
        }
        e.info.name = e.backend->name();
        e.info.vendor = e.backend->vendor();
        gpus_.push_back(std::move(e));
    }

    Nvml& n = Nvml::get();
    if (n.ok()) {
        unsigned int count = 0;
        n.getCount(&count);
        for (unsigned int i = 0; i < count; ++i) {
            nvmlDevice_t d;
            if (n.getHandle(i, &d) != 0) continue;
            char nm[96] = "NVIDIA GPU";
            if (n.getName) n.getName(d, nm, sizeof(nm));
            Entry e;
            e.info.id = "nvml:" + std::to_string(i);
            nvmlPciInfo_t pci{};
            if (n.getPci && n.getPci(d, &pci) == 0) {
                std::string bus = pci.busId;
                std::transform(bus.begin(), bus.end(), bus.begin(), ::tolower);
                // NVML usa domínio com 8 dígitos ("00000000:01:00.0")
                if (bus.size() == 16) bus = bus.substr(4);
                if (!bus.empty()) e.info.id = "pci:" + bus;
            }
            e.backend = std::make_unique<NvidiaBackend>(d, nm);
            e.info.name = nm;
            e.info.vendor = "NVIDIA";
            gpus_.push_back(std::move(e));
            nvidiaAdded = true;
        }
    }
    (void)nvidiaAdded;
    select("");
}

GpuMonitor::~GpuMonitor() = default;

std::vector<GpuInfo> GpuMonitor::list() const {
    std::vector<GpuInfo> out;
    for (const auto& e : gpus_) out.push_back(e.info);
    return out;
}

void GpuMonitor::select(const std::string& id) {
    current_ = -1;
    for (size_t i = 0; i < gpus_.size(); ++i)
        if (!id.empty() && gpus_[i].info.id == id) current_ = static_cast<int>(i);
    if (current_ >= 0) return;
    // Automático: primeira GPU dedicada; senão a primeira encontrada.
    for (size_t i = 0; i < gpus_.size(); ++i)
        if (gpus_[i].backend->discrete()) {
            current_ = static_cast<int>(i);
            return;
        }
    if (!gpus_.empty()) current_ = 0;
}

GpuSample GpuMonitor::sample() {
    if (current_ < 0) return {};
    return gpus_[static_cast<size_t>(current_)].backend->sample();
}

std::string GpuMonitor::currentName() const {
    if (current_ < 0) return {};
    return gpus_[static_cast<size_t>(current_)].info.name;
}

}  // namespace linosd
