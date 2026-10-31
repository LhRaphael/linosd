#include "metrics.h"

#include <QLocale>
#include <QtMath>
#include <cmath>
#include <random>

using linosd::has;

MetricsCollector::MetricsCollector(QObject* parent)
    : QObject(parent),
      cpu_(std::make_unique<linosd::CpuMonitor>()),
      mem_(std::make_unique<linosd::MemMonitor>()),
      gpu_(std::make_unique<linosd::GpuMonitor>()),
      bat_(std::make_unique<linosd::BatteryMonitor>()),
      frames_(std::make_unique<linosd::FrameSource>()) {
    snap_.cpuName = QString::fromStdString(cpu_->name());
    snap_.gpuName = QString::fromStdString(gpu_->currentName());
    snap_.hasBattery = bat_->present();
    connect(&sensorTimer_, &QTimer::timeout, this, &MetricsCollector::pollSensors);
    connect(&frameTimer_, &QTimer::timeout, this, &MetricsCollector::pollFrames);
    applySettings(settings_);
    pollSensors();
    pollFrames();
}

MetricsCollector::~MetricsCollector() = default;

void MetricsCollector::applySettings(const GeneralSettings& g) {
    settings_ = g;
    gpu_->select(g.gpuId.toStdString());
    snap_.gpuName = QString::fromStdString(gpu_->currentName());
    frames_->setFpsLimit(static_cast<uint32_t>(qMax(0, g.fpsLimit)));
    frames_->setLowsMethodAverage(g.lowsAverage);
    sensorTimer_.start(g.sensorMs);
    frameTimer_.start(g.refreshMs);
}

void MetricsCollector::pollSensors() {
    snap_.cpu = cpu_->sample();
    snap_.mem = mem_->sample();
    snap_.gpu = gpu_->sample();
    if (snap_.hasBattery) snap_.battery = bat_->sample();
}

void MetricsCollector::pollFrames() {
    snap_.frames = frames_->sample(settings_.fpsWindowMs, settings_.lowsWindowS, settings_.graphFrames);
    snap_.now = QDateTime::currentDateTime();
    emit updated();
}

// ---------------------------------------------------------------------------
// Dados de demonstração
// ---------------------------------------------------------------------------
linosd::FrameStats demoFrameStats(int graphFrames) {
    static std::mt19937 rng(42);
    static std::vector<float> history;
    std::normal_distribution<float> noise(0.f, 0.35f);
    std::uniform_real_distribution<float> spike(0.f, 1.f);
    const float base = 1000.f / 144.f;
    int add = 3;
    for (int i = 0; i < add; ++i) {
        float v = base + noise(rng);
        if (spike(rng) < 0.02f) v += 6.f + spike(rng) * 10.f;
        history.push_back(std::max(1.f, v));
    }
    if (static_cast<int>(history.size()) > 4000) history.erase(history.begin(), history.end() - 2000);

    linosd::FrameStats st;
    st.active = true;
    st.process = "Demo.exe";
    st.api = "Vulkan";
    st.width = 2560;
    st.height = 1440;
    st.sessionS = 754;
    size_t n = std::min(history.size(), static_cast<size_t>(std::max(2, graphFrames)));
    st.frametimes.assign(history.end() - static_cast<long>(n), history.end());
    double sum = 0;
    size_t win = std::min<size_t>(history.size(), 72);
    for (size_t i = history.size() - win; i < history.size(); ++i) sum += history[i];
    st.frametimeMs = sum / static_cast<double>(win);
    st.fps = 1000.0 / st.frametimeMs;
    std::vector<float> sorted(history.end() - static_cast<long>(std::min<size_t>(history.size(), 1440)), history.end());
    std::sort(sorted.begin(), sorted.end());
    st.low1 = 1000.0 / sorted[static_cast<size_t>(0.99 * static_cast<double>(sorted.size() - 1))];
    st.low01 = 1000.0 / sorted[static_cast<size_t>(0.999 * static_cast<double>(sorted.size() - 1))];
    st.minFps = 1000.0 / sorted.back();
    st.maxFps = 1000.0 / sorted.front();
    return st;
}

// ---------------------------------------------------------------------------
// Formatação
// ---------------------------------------------------------------------------
namespace {

QString num(double v, int decimals) {
    if (!has(v)) return QStringLiteral("--");
    return QString::number(v, 'f', decimals);
}

QString duration(double secs) {
    if (!has(secs)) return QStringLiteral("--");
    qint64 s = static_cast<qint64>(secs);
    return QString("%1:%2:%3")
        .arg(s / 3600, 2, 10, QChar('0'))
        .arg((s / 60) % 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}

}  // namespace

FormattedValue formatMetric(const OsdItem& item, const MetricsSnapshot& s, const GeneralSettings& g) {
    const MetricInfo& info = metricInfo(item.type);
    FormattedValue out;
    out.unit = QString::fromUtf8(info.unit);
    int dec = item.decimals >= 0 ? item.decimals : info.decimals;
    double v = linosd::kNaN;

    auto temp = [&](double c) {
        if (g.fahrenheit) {
            out.unit = QStringLiteral("°F");
            return has(c) ? c * 9.0 / 5.0 + 32.0 : c;
        }
        return c;
    };
    auto mem = [&](double mib) {
        if (g.memoryInMB) {
            out.unit = QStringLiteral("MB");
            if (item.decimals < 0) dec = 0;
            return mib;
        }
        return has(mib) ? mib / 1024.0 : mib;
    };

    const auto& f = s.frames;
    switch (item.type) {
        case MetricType::Fps: v = f.fps; break;
        case MetricType::FrameTime: v = f.frametimeMs; break;
        case MetricType::Low1: v = f.low1; break;
        case MetricType::Low01: v = f.low01; break;
        case MetricType::MinFps: v = f.minFps; break;
        case MetricType::MaxFps: v = f.maxFps; break;
        case MetricType::FpsLimit:
            if (f.active && f.fpsLimit == 0) {
                out.value = QStringLiteral("OFF");
                out.unit.clear();
                out.valid = true;
                return out;
            }
            v = f.active ? static_cast<double>(f.fpsLimit) : linosd::kNaN;
            break;
        case MetricType::FrametimeGraph: out.valid = !f.frametimes.empty(); return out;
        case MetricType::CpuUsage: v = s.cpu.usage; break;
        case MetricType::CpuMaxCore: v = s.cpu.maxCoreUsage; break;
        case MetricType::CpuTemp: v = temp(s.cpu.temp); break;
        case MetricType::CpuFreq: v = s.cpu.freqMHz; break;
        case MetricType::CpuPower: v = s.cpu.powerW; break;
        case MetricType::RamUsed: v = mem(s.mem.usedMiB); break;
        case MetricType::RamPercent:
            v = (has(s.mem.usedMiB) && s.mem.totalMiB > 0) ? 100.0 * s.mem.usedMiB / s.mem.totalMiB : linosd::kNaN;
            break;
        case MetricType::SwapUsed: v = mem(s.mem.swapUsedMiB); break;
        case MetricType::GpuUsage: v = s.gpu.usage; break;
        case MetricType::GpuTemp: v = temp(s.gpu.temp); break;
        case MetricType::GpuHotspot: v = temp(s.gpu.hotspot); break;
        case MetricType::GpuCoreClock: v = s.gpu.coreMHz; break;
        case MetricType::GpuMemClock: v = s.gpu.memMHz; break;
        case MetricType::GpuPower: v = s.gpu.powerW; break;
        case MetricType::VramUsed: v = mem(s.gpu.vramUsedMiB); break;
        case MetricType::VramPercent:
            v = (has(s.gpu.vramUsedMiB) && s.gpu.vramTotalMiB > 0) ? 100.0 * s.gpu.vramUsedMiB / s.gpu.vramTotalMiB
                                                                   : linosd::kNaN;
            break;
        case MetricType::GpuFan:
            if (has(s.gpu.fanPercent)) {
                v = s.gpu.fanPercent;
            } else if (has(s.gpu.fanRpm)) {
                v = s.gpu.fanRpm;
                out.unit = QStringLiteral("RPM");
            }
            break;
        case MetricType::Battery: v = s.battery.percent; break;
        case MetricType::BatteryPower: v = s.battery.powerW; break;
        case MetricType::Clock:
            out.value = s.now.toString(item.text.isEmpty() ? QStringLiteral("HH:mm:ss") : item.text);
            out.valid = true;
            return out;
        case MetricType::SessionTime:
            out.value = duration(f.sessionS);
            out.valid = has(f.sessionS);
            return out;
        case MetricType::ProcessName:
            out.value = f.active ? QString::fromStdString(f.process) : QStringLiteral("--");
            out.valid = f.active;
            return out;
        case MetricType::Api:
            out.value = f.active ? QString::fromStdString(f.api) : QStringLiteral("--");
            out.valid = f.active;
            return out;
        case MetricType::Resolution:
            out.valid = f.width > 0 && f.height > 0;
            out.value = out.valid ? QString("%1x%2").arg(f.width).arg(f.height) : QStringLiteral("--");
            return out;
        case MetricType::GpuName:
            out.value = s.gpuName.isEmpty() ? QStringLiteral("GPU não detectada") : s.gpuName;
            out.valid = true;
            return out;
        case MetricType::CpuName:
            out.value = s.cpuName;
            out.valid = true;
            return out;
        case MetricType::CustomText:
            out.value = item.text;
            out.valid = true;
            return out;
        case MetricType::Count: break;
    }
    out.numeric = v;
    out.valid = has(v);
    out.value = num(v, dec);
    return out;
}
