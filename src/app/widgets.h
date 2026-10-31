// Widgets auxiliares da interface: seletor de cor, editor de fonte e prévia.
#pragma once

#include <QImage>
#include <QToolButton>
#include <QWidget>

#include "config.h"

class QFontComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;

class ColorButton : public QToolButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget* parent = nullptr);
    QColor color() const { return color_; }
    void setColor(const QColor& c);

signals:
    void colorChanged(const QColor& c);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QColor color_{255, 255, 255};
};

class FontEditor : public QWidget {
    Q_OBJECT
public:
    explicit FontEditor(QWidget* parent = nullptr);
    FontSpec spec() const;
    void setSpec(const FontSpec& f);

signals:
    void changed();

private:
    QFontComboBox* family_;
    QDoubleSpinBox* size_;
    QToolButton* bold_;
    QToolButton* italic_;
    bool blocking_ = false;
};

// Mostra o OSD sobre uma "cena de jogo" para avaliar contraste e efeitos.
class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    void setImage(const QImage& img, Anchor anchor);
    void setScene(int scene);
    QSize sizeHint() const override { return QSize(520, 300); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QImage image_;
    Anchor anchor_ = Anchor::TopLeft;
    int scene_ = 0;
};

// Ícone do aplicativo desenhado em código (sem depender de arquivos externos).
QIcon makeAppIcon();
