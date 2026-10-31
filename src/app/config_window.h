// Janela principal de configuração (personalização completa do OSD).
#pragma once

#include <QMainWindow>
#include <QTimer>

#include "config.h"

class QListWidget;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QSlider;
class QButtonGroup;
class QPushButton;
class QMenu;
class ColorButton;
class FontEditor;
class PreviewWidget;
class MetricsCollector;
class OverlayWindow;

class ConfigWindow : public QMainWindow {
    Q_OBJECT
public:
    ConfigWindow(const Config& cfg, MetricsCollector* metrics, OverlayWindow* overlay, QWidget* parent = nullptr);
    const Config& config() const { return cfg_; }
    void setHideOnClose(bool v) { hideOnClose_ = v; }
    void setFpsLimit(int fps);  // usado pelo comando remoto --fps-limit

signals:
    void configChanged(const Config& cfg);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    QWidget* buildItemsTab();
    QWidget* buildAppearanceTab();
    QWidget* buildPositionTab();
    QWidget* buildGeneralTab();
    QWidget* buildPreviewPanel();

    void loadToUi();
    void refreshItemList(int select = -1);
    QString itemText(const OsdItem& it) const;
    void loadItemEditor();
    void readItemEditor();
    void readStyle();
    void readPosition();
    void readGeneral();
    void changed();
    void updatePreview();
    void updateStatus();
    void addItem(MetricType t);
    void moveItem(int delta);
    void onRowsMoved();
    void setAutostart(bool on);
    bool autostartEnabled() const;

    Config cfg_;
    MetricsCollector* metrics_;
    OverlayWindow* overlay_;
    bool loading_ = false;
    bool hideOnClose_ = true;
    QTimer saveTimer_;

    // Itens
    QListWidget* itemList_;
    QWidget* itemEditor_;
    QLabel* itemMetric_;
    QLineEdit* itemLabel_;
    QCheckBox* itemShowLabel_;
    QCheckBox* itemNewLine_;
    QCheckBox* itemShowUnit_;
    QSpinBox* itemDecimals_;
    QCheckBox* itemCustomLabelColor_;
    ColorButton* itemLabelColor_;
    QCheckBox* itemCustomValueColor_;
    ColorButton* itemValueColor_;
    QCheckBox* itemCustomUnitColor_;
    ColorButton* itemUnitColor_;
    QGroupBox* alertGroup_;
    QComboBox* alertCond_;
    QDoubleSpinBox* alertThreshold_;
    ColorButton* alertColor_;
    QGroupBox* graphGroup_;
    QSpinBox* graphW_;
    QSpinBox* graphH_;
    QDoubleSpinBox* graphMax_;
    QDoubleSpinBox* graphLine_;
    QCheckBox* graphFill_;
    ColorButton* graphBg_;
    QGroupBox* textGroup_;
    QLineEdit* itemText_;
    QLabel* itemTextHelp_;

    // Aparência
    FontEditor* labelFont_;
    FontEditor* valueFont_;
    FontEditor* unitFont_;
    ColorButton* labelColor_;
    ColorButton* valueColor_;
    ColorButton* unitColor_;
    QGroupBox* outlineGroup_;
    ColorButton* outlineColor_;
    QDoubleSpinBox* outlineWidth_;
    QGroupBox* shadowGroup_;
    ColorButton* shadowColor_;
    QSpinBox* shadowX_;
    QSpinBox* shadowY_;
    QSpinBox* shadowBlur_;
    QGroupBox* bgGroup_;
    ColorButton* bgColor_;
    QSpinBox* bgRadius_;
    QSpinBox* bgPadding_;
    QCheckBox* borderEnabled_;
    ColorButton* borderColor_;
    QDoubleSpinBox* borderWidth_;
    QComboBox* layout_;
    QSpinBox* lineSpacing_;
    QSpinBox* itemSpacing_;
    QCheckBox* alignLabels_;
    QCheckBox* fixedWidth_;
    QSlider* opacity_;
    QLabel* opacityLabel_;
    QSpinBox* scale_;

    // Posição
    QComboBox* screen_;
    QButtonGroup* anchorGroup_;
    QSpinBox* offsetX_;
    QSpinBox* offsetY_;
    QPushButton* moveMode_;

    // Geral
    QComboBox* displayMode_;
    QSpinBox* refreshMs_;
    QSpinBox* sensorMs_;
    QSpinBox* fpsWindow_;
    QSpinBox* lowsWindow_;
    QComboBox* lowsMethod_;
    QSpinBox* graphFrames_;
    QSpinBox* fpsLimit_;
    QComboBox* gpu_;
    QComboBox* tempUnit_;
    QComboBox* memUnit_;
    QCheckBox* startMinimized_;
    QCheckBox* autostart_;

    // Prévia / status
    PreviewWidget* preview_;
    QComboBox* scene_;
    QCheckBox* overlayVisible_;
    QLabel* status_;
    QComboBox* preset_;
};
