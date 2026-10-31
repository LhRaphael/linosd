// Configuração do LinOSD: itens exibidos, estilo, posição e opções gerais.
// Persistida em ~/.config/linosd/config.json.
#pragma once

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QString>
#include <QVector>

enum class MetricType {
    Fps,
    FrameTime,
    Low1,
    Low01,
    MinFps,
    MaxFps,
    FrametimeGraph,
    FpsLimit,
    CpuUsage,
    CpuMaxCore,
    CpuTemp,
    CpuFreq,
    CpuPower,
    RamUsed,
    RamPercent,
    SwapUsed,
    GpuUsage,
    GpuTemp,
    GpuHotspot,
    GpuCoreClock,
    GpuMemClock,
    GpuPower,
    VramUsed,
    VramPercent,
    GpuFan,
    Battery,
    BatteryPower,
    Clock,
    SessionTime,
    ProcessName,
    Api,
    Resolution,
    GpuName,
    CpuName,
    CustomText,
    Count
};

enum class MetricKind { Number, Text, Graph };

struct MetricInfo {
    MetricType type;
    const char* key;          // identificador no JSON
    const char* displayName;  // nome na interface
    const char* category;
    const char* defaultLabel;
    const char* unit;
    MetricKind kind;
    int decimals;             // casas decimais padrão
    const char* widthTemplate;// reserva de largura para valores (evita tremulação)
};

const MetricInfo& metricInfo(MetricType t);
MetricType metricFromKey(const QString& key, bool* ok = nullptr);

struct OsdItem {
    MetricType type = MetricType::Fps;
    bool enabled = true;
    bool newLine = true;      // inicia uma nova linha (layout vertical)
    QString label;
    bool showLabel = true;
    bool showUnit = true;
    int decimals = -1;        // -1 = padrão da métrica

    bool customLabelColor = false;
    QColor labelColor{255, 255, 255};
    bool customValueColor = false;
    QColor valueColor{255, 255, 255};
    bool customUnitColor = false;
    QColor unitColor{200, 200, 200};

    bool alertEnabled = false;
    bool alertBelow = false;  // false = alerta acima do limite
    double alertThreshold = 80;
    QColor alertColor{255, 70, 70};

    // Gráfico de frametime
    int graphWidth = 240;
    int graphHeight = 50;
    double graphMaxMs = 0;    // 0 = automático
    double graphLineWidth = 1.5;
    bool graphFill = true;
    QColor graphBgColor{0, 0, 0, 90};

    QString text;             // texto personalizado / formato do relógio

    static OsdItem make(MetricType t, bool newLine = true, const QString& label = QString());
    QJsonObject toJson() const;
    static OsdItem fromJson(const QJsonObject& o);
};

struct FontSpec {
    QString family = "Sans Serif";
    double size = 13;
    bool bold = true;
    bool italic = false;
    QFont toFont(double scale = 1.0) const;
    QJsonObject toJson() const;
    static FontSpec fromJson(const QJsonObject& o, const FontSpec& def);
};

enum class OsdLayout { Vertical = 0, Horizontal = 1 };
enum class DisplayMode { Always = 0, GameOnly = 1 };

struct OsdStyle {
    FontSpec labelFont;
    FontSpec valueFont;
    FontSpec unitFont;
    QColor labelColor{120, 200, 255};
    QColor valueColor{255, 255, 255};
    QColor unitColor{190, 190, 190};

    bool outline = true;
    QColor outlineColor{0, 0, 0, 230};
    double outlineWidth = 1.5;

    bool shadow = true;
    QColor shadowColor{0, 0, 0, 160};
    int shadowX = 2;
    int shadowY = 2;
    int shadowBlur = 3;

    bool background = true;
    QColor backgroundColor{10, 10, 14, 120};
    int cornerRadius = 8;
    int padding = 8;
    bool border = false;
    QColor borderColor{255, 255, 255, 60};
    double borderWidth = 1;

    int lineSpacing = 2;
    int itemSpacing = 10;
    bool alignLabels = true;
    bool fixedValueWidth = true;
    double opacity = 1.0;
    double scale = 1.0;
    OsdLayout layout = OsdLayout::Vertical;
};

enum class Anchor { TopLeft = 0, TopCenter, TopRight, MiddleLeft, Center, MiddleRight, BottomLeft, BottomCenter, BottomRight };

struct OsdPosition {
    QString screen;   // nome do monitor; vazio = primário
    Anchor anchor = Anchor::TopLeft;
    int offsetX = 16;
    int offsetY = 16;
};

struct GeneralSettings {
    DisplayMode displayMode = DisplayMode::GameOnly;
    int refreshMs = 100;      // repintura do overlay
    int sensorMs = 500;       // leitura de sensores
    int fpsWindowMs = 500;    // janela da média de FPS
    int lowsWindowS = 10;     // janela dos lows
    bool lowsAverage = false; // false = percentil
    int graphFrames = 150;
    int fpsLimit = 0;
    QString gpuId;            // vazio = automático
    bool fahrenheit = false;
    bool memoryInMB = false;
    bool startMinimized = false;
};

struct Config {
    QVector<OsdItem> items;
    OsdStyle style;
    OsdPosition position;
    GeneralSettings general;

    static Config defaults();
    static QStringList presetNames();
    static Config preset(const QString& name, const Config& base);

    QJsonObject toJson() const;
    static Config fromJson(const QJsonObject& o);

    static QString configPath();
    static Config load();
    bool save() const;
    bool saveTo(const QString& path) const;
    static bool loadFrom(const QString& path, Config& out);
};
