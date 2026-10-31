#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

const MetricInfo kMetrics[] = {
    {MetricType::Fps, "fps", "FPS", "Frames", "FPS", "FPS", MetricKind::Number, 0, "000"},
    {MetricType::FrameTime, "frametime", "Frametime", "Frames", "FT", "ms", MetricKind::Number, 1, "00.0"},
    {MetricType::Low1, "low1", "1% low", "Frames", "1%", "FPS", MetricKind::Number, 0, "000"},
    {MetricType::Low01, "low01", "0.1% low", "Frames", "0.1%", "FPS", MetricKind::Number, 0, "000"},
    {MetricType::MinFps, "min_fps", "FPS mínimo", "Frames", "MIN", "FPS", MetricKind::Number, 0, "000"},
    {MetricType::MaxFps, "max_fps", "FPS máximo", "Frames", "MAX", "FPS", MetricKind::Number, 0, "000"},
    {MetricType::FrametimeGraph, "frametime_graph", "Gráfico de frametime", "Frames", "", "", MetricKind::Graph, 0, ""},
    {MetricType::FpsLimit, "fps_limit", "Limite de FPS", "Frames", "LIMITE", "FPS", MetricKind::Number, 0, "000"},
    {MetricType::CpuUsage, "cpu_usage", "Uso da CPU", "CPU", "CPU", "%", MetricKind::Number, 0, "00"},
    {MetricType::CpuMaxCore, "cpu_max_core", "Núcleo mais usado", "CPU", "CORE", "%", MetricKind::Number, 0, "00"},
    {MetricType::CpuTemp, "cpu_temp", "Temperatura da CPU", "CPU", "", "°C", MetricKind::Number, 0, "00"},
    {MetricType::CpuFreq, "cpu_freq", "Clock da CPU", "CPU", "", "MHz", MetricKind::Number, 0, "0000"},
    {MetricType::CpuPower, "cpu_power", "Consumo da CPU", "CPU", "", "W", MetricKind::Number, 0, "000"},
    {MetricType::RamUsed, "ram_used", "RAM usada", "Memória", "RAM", "GB", MetricKind::Number, 1, "00.0"},
    {MetricType::RamPercent, "ram_percent", "RAM usada (%)", "Memória", "RAM", "%", MetricKind::Number, 0, "00"},
    {MetricType::SwapUsed, "swap_used", "Swap usada", "Memória", "SWAP", "GB", MetricKind::Number, 1, "00.0"},
    {MetricType::GpuUsage, "gpu_usage", "Uso da GPU", "GPU", "GPU", "%", MetricKind::Number, 0, "00"},
    {MetricType::GpuTemp, "gpu_temp", "Temperatura da GPU", "GPU", "", "°C", MetricKind::Number, 0, "00"},
    {MetricType::GpuHotspot, "gpu_hotspot", "Hotspot da GPU", "GPU", "HOT", "°C", MetricKind::Number, 0, "00"},
    {MetricType::GpuCoreClock, "gpu_core_clock", "Clock do núcleo (GPU)", "GPU", "", "MHz", MetricKind::Number, 0, "0000"},
    {MetricType::GpuMemClock, "gpu_mem_clock", "Clock da memória (GPU)", "GPU", "MEM", "MHz", MetricKind::Number, 0, "0000"},
    {MetricType::GpuPower, "gpu_power", "Consumo da GPU", "GPU", "", "W", MetricKind::Number, 0, "000"},
    {MetricType::VramUsed, "vram_used", "VRAM usada", "GPU", "VRAM", "GB", MetricKind::Number, 1, "00.0"},
    {MetricType::VramPercent, "vram_percent", "VRAM usada (%)", "GPU", "VRAM", "%", MetricKind::Number, 0, "00"},
    {MetricType::GpuFan, "gpu_fan", "Ventoinha da GPU", "GPU", "FAN", "%", MetricKind::Number, 0, "00"},
    {MetricType::Battery, "battery", "Bateria", "Sistema", "BAT", "%", MetricKind::Number, 0, "00"},
    {MetricType::BatteryPower, "battery_power", "Consumo da bateria", "Sistema", "", "W", MetricKind::Number, 1, "00.0"},
    {MetricType::Clock, "clock", "Relógio", "Sistema", "", "", MetricKind::Text, 0, ""},
    {MetricType::SessionTime, "session_time", "Tempo de jogo", "Sistema", "TEMPO", "", MetricKind::Text, 0, ""},
    {MetricType::ProcessName, "process", "Nome do jogo", "Jogo", "", "", MetricKind::Text, 0, ""},
    {MetricType::Api, "api", "API gráfica", "Jogo", "", "", MetricKind::Text, 0, ""},
    {MetricType::Resolution, "resolution", "Resolução (Vulkan)", "Jogo", "", "", MetricKind::Text, 0, ""},
    {MetricType::GpuName, "gpu_name", "Nome da GPU", "GPU", "", "", MetricKind::Text, 0, ""},
    {MetricType::CpuName, "cpu_name", "Nome da CPU", "CPU", "", "", MetricKind::Text, 0, ""},
    {MetricType::CustomText, "custom_text", "Texto personalizado", "Outros", "", "", MetricKind::Text, 0, ""},
};
static_assert(sizeof(kMetrics) / sizeof(kMetrics[0]) == static_cast<size_t>(MetricType::Count),
              "tabela de métricas incompleta");

QString colorToString(const QColor& c) { return c.name(QColor::HexArgb); }
QColor colorFrom(const QJsonValue& v, const QColor& def) {
    if (!v.isString()) return def;
    QColor c(v.toString());
    return c.isValid() ? c : def;
}

}  // namespace

const MetricInfo& metricInfo(MetricType t) { return kMetrics[static_cast<int>(t)]; }

MetricType metricFromKey(const QString& key, bool* ok) {
    for (const auto& m : kMetrics)
        if (key == QLatin1String(m.key)) {
            if (ok) *ok = true;
            return m.type;
        }
    if (ok) *ok = false;
    return MetricType::CustomText;
}

// ---------------------------------------------------------------------------
// OsdItem
// ---------------------------------------------------------------------------
OsdItem OsdItem::make(MetricType t, bool newLine, const QString& label) {
    OsdItem it;
    it.type = t;
    it.newLine = newLine;
    it.label = label.isNull() ? QString::fromUtf8(metricInfo(t).defaultLabel) : label;
    if (t == MetricType::Clock) it.text = "HH:mm:ss";
    if (t == MetricType::CustomText) it.text = "LinOSD";
    // Alertas úteis por padrão (desabilitados).
    switch (t) {
        case MetricType::CpuTemp:
        case MetricType::GpuTemp: it.alertThreshold = 85; break;
        case MetricType::GpuHotspot: it.alertThreshold = 100; break;
        case MetricType::Fps:
        case MetricType::Low1:
        case MetricType::Low01:
            it.alertThreshold = 30;
            it.alertBelow = true;
            break;
        case MetricType::Battery:
            it.alertThreshold = 20;
            it.alertBelow = true;
            break;
        default: it.alertThreshold = 90; break;
    }
    return it;
}

QJsonObject OsdItem::toJson() const {
    QJsonObject o;
    o["type"] = QString::fromLatin1(metricInfo(type).key);
    o["enabled"] = enabled;
    o["new_line"] = newLine;
    o["label"] = label;
    o["show_label"] = showLabel;
    o["show_unit"] = showUnit;
    o["decimals"] = decimals;
    o["custom_label_color"] = customLabelColor;
    o["label_color"] = colorToString(labelColor);
    o["custom_value_color"] = customValueColor;
    o["value_color"] = colorToString(valueColor);
    o["custom_unit_color"] = customUnitColor;
    o["unit_color"] = colorToString(unitColor);
    o["alert_enabled"] = alertEnabled;
    o["alert_below"] = alertBelow;
    o["alert_threshold"] = alertThreshold;
    o["alert_color"] = colorToString(alertColor);
    o["graph_width"] = graphWidth;
    o["graph_height"] = graphHeight;
    o["graph_max_ms"] = graphMaxMs;
    o["graph_line_width"] = graphLineWidth;
    o["graph_fill"] = graphFill;
    o["graph_bg_color"] = colorToString(graphBgColor);
    o["text"] = text;
    return o;
}

OsdItem OsdItem::fromJson(const QJsonObject& o) {
    bool ok = false;
    MetricType t = metricFromKey(o["type"].toString(), &ok);
    OsdItem it = make(t);
    if (!ok) {
        it.enabled = false;
        return it;
    }
    it.enabled = o["enabled"].toBool(it.enabled);
    it.newLine = o["new_line"].toBool(it.newLine);
    it.label = o["label"].toString(it.label);
    it.showLabel = o["show_label"].toBool(it.showLabel);
    it.showUnit = o["show_unit"].toBool(it.showUnit);
    it.decimals = o["decimals"].toInt(it.decimals);
    it.customLabelColor = o["custom_label_color"].toBool(false);
    it.labelColor = colorFrom(o["label_color"], it.labelColor);
    it.customValueColor = o["custom_value_color"].toBool(false);
    it.valueColor = colorFrom(o["value_color"], it.valueColor);
    it.customUnitColor = o["custom_unit_color"].toBool(false);
    it.unitColor = colorFrom(o["unit_color"], it.unitColor);
    it.alertEnabled = o["alert_enabled"].toBool(false);
    it.alertBelow = o["alert_below"].toBool(it.alertBelow);
    it.alertThreshold = o["alert_threshold"].toDouble(it.alertThreshold);
    it.alertColor = colorFrom(o["alert_color"], it.alertColor);
    it.graphWidth = o["graph_width"].toInt(it.graphWidth);
    it.graphHeight = o["graph_height"].toInt(it.graphHeight);
    it.graphMaxMs = o["graph_max_ms"].toDouble(it.graphMaxMs);
    it.graphLineWidth = o["graph_line_width"].toDouble(it.graphLineWidth);
    it.graphFill = o["graph_fill"].toBool(it.graphFill);
    it.graphBgColor = colorFrom(o["graph_bg_color"], it.graphBgColor);
    it.text = o["text"].toString(it.text);
    return it;
}

// ---------------------------------------------------------------------------
// FontSpec
// ---------------------------------------------------------------------------
QFont FontSpec::toFont(double scale) const {
    QFont f(family);
    f.setPointSizeF(qMax(4.0, size * scale));
    f.setBold(bold);
    f.setItalic(italic);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QJsonObject FontSpec::toJson() const {
    return QJsonObject{{"family", family}, {"size", size}, {"bold", bold}, {"italic", italic}};
}

FontSpec FontSpec::fromJson(const QJsonObject& o, const FontSpec& def) {
    FontSpec f = def;
    f.family = o["family"].toString(def.family);
    f.size = o["size"].toDouble(def.size);
    f.bold = o["bold"].toBool(def.bold);
    f.italic = o["italic"].toBool(def.italic);
    return f;
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
Config Config::defaults() {
    Config c;
    c.style.labelFont = {"Sans Serif", 11, true, false};
    c.style.valueFont = {"Sans Serif", 14, true, false};
    c.style.unitFont = {"Sans Serif", 9, true, false};

    // Estilo "Afterburner": uma linha por componente.
    c.items = {
        OsdItem::make(MetricType::GpuUsage, true, "GPU"),
        OsdItem::make(MetricType::GpuTemp, false),
        OsdItem::make(MetricType::GpuCoreClock, false),
        OsdItem::make(MetricType::GpuPower, false),
        OsdItem::make(MetricType::VramUsed, true, "VRAM"),
        OsdItem::make(MetricType::CpuUsage, true, "CPU"),
        OsdItem::make(MetricType::CpuTemp, false),
        OsdItem::make(MetricType::CpuFreq, false),
        OsdItem::make(MetricType::RamUsed, true, "RAM"),
        OsdItem::make(MetricType::Fps, true, "FPS"),
        OsdItem::make(MetricType::FrameTime, false, ""),
        OsdItem::make(MetricType::Low1, true, "1% LOW"),
        OsdItem::make(MetricType::Low01, false, "0.1%"),
        OsdItem::make(MetricType::FrametimeGraph, true, ""),
    };
    // Cores por componente (como no Afterburner/RTSS)
    auto color = [&](MetricType t, QColor col) {
        for (auto& it : c.items)
            if (it.type == t) {
                it.customLabelColor = true;
                it.labelColor = col;
            }
    };
    color(MetricType::GpuUsage, QColor(120, 220, 120));
    color(MetricType::VramUsed, QColor(120, 220, 120));
    color(MetricType::CpuUsage, QColor(90, 180, 255));
    color(MetricType::RamUsed, QColor(255, 130, 200));
    color(MetricType::Fps, QColor(255, 200, 60));
    color(MetricType::Low1, QColor(255, 200, 60));
    for (auto& it : c.items) {
        if (it.type == MetricType::FrametimeGraph) {
            it.customValueColor = true;
            it.valueColor = QColor(255, 200, 60);
        }
        if (it.type == MetricType::Low01) {
            it.customLabelColor = true;
            it.labelColor = QColor(255, 200, 60);
        }
    }
    return c;
}

QStringList Config::presetNames() {
    return {"Afterburner clássico", "Completo", "Minimalista (só FPS)", "Horizontal (barra)", "Neon"};
}

Config Config::preset(const QString& name, const Config& base) {
    Config c = defaults();
    c.position = base.position;
    c.general = base.general;
    if (name == "Completo") {
        c.items = {
            OsdItem::make(MetricType::ProcessName, true, ""),
            OsdItem::make(MetricType::Api, false, ""),
            OsdItem::make(MetricType::Resolution, false, ""),
            OsdItem::make(MetricType::GpuName, true, ""),
            OsdItem::make(MetricType::GpuUsage, true, "GPU"),
            OsdItem::make(MetricType::GpuTemp, false),
            OsdItem::make(MetricType::GpuHotspot, false),
            OsdItem::make(MetricType::GpuCoreClock, false),
            OsdItem::make(MetricType::GpuMemClock, false),
            OsdItem::make(MetricType::GpuPower, false),
            OsdItem::make(MetricType::GpuFan, false),
            OsdItem::make(MetricType::VramUsed, true, "VRAM"),
            OsdItem::make(MetricType::VramPercent, false, ""),
            OsdItem::make(MetricType::CpuName, true, ""),
            OsdItem::make(MetricType::CpuUsage, true, "CPU"),
            OsdItem::make(MetricType::CpuMaxCore, false, "MAX"),
            OsdItem::make(MetricType::CpuTemp, false),
            OsdItem::make(MetricType::CpuFreq, false),
            OsdItem::make(MetricType::CpuPower, false),
            OsdItem::make(MetricType::RamUsed, true, "RAM"),
            OsdItem::make(MetricType::RamPercent, false, ""),
            OsdItem::make(MetricType::SwapUsed, false, "SWAP"),
            OsdItem::make(MetricType::Fps, true, "FPS"),
            OsdItem::make(MetricType::FrameTime, false, ""),
            OsdItem::make(MetricType::Low1, true, "1% LOW"),
            OsdItem::make(MetricType::Low01, false, "0.1%"),
            OsdItem::make(MetricType::MinFps, true, "MIN"),
            OsdItem::make(MetricType::MaxFps, false, "MAX"),
            OsdItem::make(MetricType::FrametimeGraph, true, ""),
            OsdItem::make(MetricType::Clock, true, ""),
            OsdItem::make(MetricType::SessionTime, false),
        };
        for (auto& it : c.items) {
            if (it.type == MetricType::GpuTemp || it.type == MetricType::CpuTemp || it.type == MetricType::GpuHotspot)
                it.alertEnabled = true;
            if (it.type == MetricType::ProcessName || it.type == MetricType::GpuName || it.type == MetricType::CpuName) {
                it.customValueColor = true;
                it.valueColor = QColor(200, 200, 200);
            }
        }
    } else if (name == "Minimalista (só FPS)") {
        c.items = {OsdItem::make(MetricType::Fps, true, "")};
        c.items[0].showUnit = true;
        c.items[0].customValueColor = true;
        c.items[0].valueColor = QColor(120, 255, 120);
        c.items[0].alertEnabled = true;
        c.style.valueFont.size = 20;
        c.style.background = false;
        c.style.outlineWidth = 2;
    } else if (name == "Horizontal (barra)") {
        c.style.layout = OsdLayout::Horizontal;
        c.style.cornerRadius = 0;
        c.style.padding = 6;
        c.style.itemSpacing = 14;
        c.items = {
            OsdItem::make(MetricType::GpuUsage, true, "GPU"), OsdItem::make(MetricType::GpuTemp, false),
            OsdItem::make(MetricType::VramUsed, true, "VRAM"), OsdItem::make(MetricType::CpuUsage, true, "CPU"),
            OsdItem::make(MetricType::CpuTemp, false), OsdItem::make(MetricType::RamUsed, true, "RAM"),
            OsdItem::make(MetricType::Fps, true, "FPS"), OsdItem::make(MetricType::FrameTime, false, ""),
            OsdItem::make(MetricType::Low1, true, "1%"),
        };
    } else if (name == "Neon") {
        c.style.labelColor = QColor(255, 60, 200);
        c.style.valueColor = QColor(80, 255, 240);
        c.style.unitColor = QColor(180, 140, 255);
        c.style.outlineColor = QColor(40, 0, 60, 255);
        c.style.outlineWidth = 2;
        c.style.shadowColor = QColor(255, 0, 200, 200);
        c.style.shadowX = 0;
        c.style.shadowY = 0;
        c.style.shadowBlur = 8;
        c.style.backgroundColor = QColor(20, 0, 40, 150);
        c.style.border = true;
        c.style.borderColor = QColor(255, 60, 200, 180);
        c.style.cornerRadius = 12;
        for (auto& it : c.items) {
            it.customLabelColor = false;
            it.customValueColor = false;
        }
    }
    return c;
}

QJsonObject Config::toJson() const {
    QJsonObject root;
    root["version"] = 1;
    QJsonArray arr;
    for (const auto& it : items) arr.append(it.toJson());
    root["items"] = arr;

    const OsdStyle& s = style;
    QJsonObject st;
    st["label_font"] = s.labelFont.toJson();
    st["value_font"] = s.valueFont.toJson();
    st["unit_font"] = s.unitFont.toJson();
    st["label_color"] = colorToString(s.labelColor);
    st["value_color"] = colorToString(s.valueColor);
    st["unit_color"] = colorToString(s.unitColor);
    st["outline"] = s.outline;
    st["outline_color"] = colorToString(s.outlineColor);
    st["outline_width"] = s.outlineWidth;
    st["shadow"] = s.shadow;
    st["shadow_color"] = colorToString(s.shadowColor);
    st["shadow_x"] = s.shadowX;
    st["shadow_y"] = s.shadowY;
    st["shadow_blur"] = s.shadowBlur;
    st["background"] = s.background;
    st["background_color"] = colorToString(s.backgroundColor);
    st["corner_radius"] = s.cornerRadius;
    st["padding"] = s.padding;
    st["border"] = s.border;
    st["border_color"] = colorToString(s.borderColor);
    st["border_width"] = s.borderWidth;
    st["line_spacing"] = s.lineSpacing;
    st["item_spacing"] = s.itemSpacing;
    st["align_labels"] = s.alignLabels;
    st["fixed_value_width"] = s.fixedValueWidth;
    st["opacity"] = s.opacity;
    st["scale"] = s.scale;
    st["layout"] = static_cast<int>(s.layout);
    root["style"] = st;

    root["position"] = QJsonObject{{"screen", position.screen},
                                   {"anchor", static_cast<int>(position.anchor)},
                                   {"offset_x", position.offsetX},
                                   {"offset_y", position.offsetY}};

    const GeneralSettings& g = general;
    root["general"] = QJsonObject{{"display_mode", static_cast<int>(g.displayMode)},
                                  {"refresh_ms", g.refreshMs},
                                  {"sensor_ms", g.sensorMs},
                                  {"fps_window_ms", g.fpsWindowMs},
                                  {"lows_window_s", g.lowsWindowS},
                                  {"lows_average", g.lowsAverage},
                                  {"graph_frames", g.graphFrames},
                                  {"fps_limit", g.fpsLimit},
                                  {"gpu_id", g.gpuId},
                                  {"fahrenheit", g.fahrenheit},
                                  {"memory_in_mb", g.memoryInMB},
                                  {"start_minimized", g.startMinimized}};
    return root;
}

Config Config::fromJson(const QJsonObject& root) {
    Config c = defaults();
    if (root.contains("items")) {
        c.items.clear();
        for (const auto& v : root["items"].toArray()) c.items.append(OsdItem::fromJson(v.toObject()));
    }
    const QJsonObject st = root["style"].toObject();
    OsdStyle& s = c.style;
    s.labelFont = FontSpec::fromJson(st["label_font"].toObject(), s.labelFont);
    s.valueFont = FontSpec::fromJson(st["value_font"].toObject(), s.valueFont);
    s.unitFont = FontSpec::fromJson(st["unit_font"].toObject(), s.unitFont);
    s.labelColor = colorFrom(st["label_color"], s.labelColor);
    s.valueColor = colorFrom(st["value_color"], s.valueColor);
    s.unitColor = colorFrom(st["unit_color"], s.unitColor);
    s.outline = st["outline"].toBool(s.outline);
    s.outlineColor = colorFrom(st["outline_color"], s.outlineColor);
    s.outlineWidth = st["outline_width"].toDouble(s.outlineWidth);
    s.shadow = st["shadow"].toBool(s.shadow);
    s.shadowColor = colorFrom(st["shadow_color"], s.shadowColor);
    s.shadowX = st["shadow_x"].toInt(s.shadowX);
    s.shadowY = st["shadow_y"].toInt(s.shadowY);
    s.shadowBlur = st["shadow_blur"].toInt(s.shadowBlur);
    s.background = st["background"].toBool(s.background);
    s.backgroundColor = colorFrom(st["background_color"], s.backgroundColor);
    s.cornerRadius = st["corner_radius"].toInt(s.cornerRadius);
    s.padding = st["padding"].toInt(s.padding);
    s.border = st["border"].toBool(s.border);
    s.borderColor = colorFrom(st["border_color"], s.borderColor);
    s.borderWidth = st["border_width"].toDouble(s.borderWidth);
    s.lineSpacing = st["line_spacing"].toInt(s.lineSpacing);
    s.itemSpacing = st["item_spacing"].toInt(s.itemSpacing);
    s.alignLabels = st["align_labels"].toBool(s.alignLabels);
    s.fixedValueWidth = st["fixed_value_width"].toBool(s.fixedValueWidth);
    s.opacity = st["opacity"].toDouble(s.opacity);
    s.scale = st["scale"].toDouble(s.scale);
    s.layout = static_cast<OsdLayout>(st["layout"].toInt(static_cast<int>(s.layout)));

    const QJsonObject pos = root["position"].toObject();
    c.position.screen = pos["screen"].toString();
    c.position.anchor = static_cast<Anchor>(qBound(0, pos["anchor"].toInt(0), 8));
    c.position.offsetX = pos["offset_x"].toInt(c.position.offsetX);
    c.position.offsetY = pos["offset_y"].toInt(c.position.offsetY);

    const QJsonObject g = root["general"].toObject();
    GeneralSettings& gs = c.general;
    gs.displayMode = static_cast<DisplayMode>(g["display_mode"].toInt(static_cast<int>(gs.displayMode)));
    gs.refreshMs = qBound(16, g["refresh_ms"].toInt(gs.refreshMs), 5000);
    gs.sensorMs = qBound(100, g["sensor_ms"].toInt(gs.sensorMs), 10000);
    gs.fpsWindowMs = qBound(100, g["fps_window_ms"].toInt(gs.fpsWindowMs), 5000);
    gs.lowsWindowS = qBound(1, g["lows_window_s"].toInt(gs.lowsWindowS), 15);
    gs.lowsAverage = g["lows_average"].toBool(gs.lowsAverage);
    gs.graphFrames = qBound(10, g["graph_frames"].toInt(gs.graphFrames), 2000);
    gs.fpsLimit = qBound(0, g["fps_limit"].toInt(gs.fpsLimit), 1000);
    gs.gpuId = g["gpu_id"].toString();
    gs.fahrenheit = g["fahrenheit"].toBool(gs.fahrenheit);
    gs.memoryInMB = g["memory_in_mb"].toBool(gs.memoryInMB);
    gs.startMinimized = g["start_minimized"].toBool(gs.startMinimized);
    return c;
}

QString Config::configPath() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/linosd";
    return dir + "/config.json";
}

bool Config::loadFrom(const QString& path, Config& out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    out = fromJson(doc.object());
    return true;
}

Config Config::load() {
    Config c;
    if (loadFrom(configPath(), c)) return c;
    return defaults();
}

bool Config::saveTo(const QString& path) const {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool Config::save() const { return saveTo(configPath()); }
