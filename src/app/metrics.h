// Coleta periódica de métricas e formatação dos valores exibidos.
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>

#include "config.h"
#include "core/frame_source.h"
#include "core/gpu_monitor.h"
#include "core/system_monitor.h"

struct MetricsSnapshot {
    linosd::FrameStats frames;
    linosd::CpuSample cpu;
    linosd::MemSample mem;
    linosd::GpuSample gpu;
    linosd::BatterySample battery;
    bool hasBattery = false;
    QString cpuName;
    QString gpuName;
    QDateTime now;
    bool demoFrames = false;  // frames sintéticos (prévia sem jogo)
};

// Valor formatado de um item.
struct FormattedValue {
    QString value;
    QString unit;
    double numeric = linosd::kNaN;  // para alertas
    bool valid = false;
};

FormattedValue formatMetric(const OsdItem& item, const MetricsSnapshot& s, const GeneralSettings& g);

// Frames sintéticos para a prévia quando nenhum jogo está aberto.
linosd::FrameStats demoFrameStats(int graphFrames);

class MetricsCollector : public QObject {
    Q_OBJECT
public:
    explicit MetricsCollector(QObject* parent = nullptr);
    ~MetricsCollector() override;

    void applySettings(const GeneralSettings& g);
    const MetricsSnapshot& snapshot() const { return snap_; }
    std::vector<linosd::GpuInfo> gpus() const { return gpu_->list(); }

signals:
    void updated();

private:
    void pollSensors();
    void pollFrames();

    std::unique_ptr<linosd::CpuMonitor> cpu_;
    std::unique_ptr<linosd::MemMonitor> mem_;
    std::unique_ptr<linosd::GpuMonitor> gpu_;
    std::unique_ptr<linosd::BatteryMonitor> bat_;
    std::unique_ptr<linosd::FrameSource> frames_;
    QTimer sensorTimer_;
    QTimer frameTimer_;
    GeneralSettings settings_;
    MetricsSnapshot snap_;
};
