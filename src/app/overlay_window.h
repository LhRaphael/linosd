// Janela do overlay: transparente, sem borda, sempre no topo e "click-through".
#pragma once

#include <QImage>
#include <QTimer>
#include <QWidget>

#include "config.h"

class MetricsCollector;

class OverlayWindow : public QWidget {
    Q_OBJECT
public:
    explicit OverlayWindow(MetricsCollector* metrics, QWidget* parent = nullptr);

    void setConfig(const Config& cfg);
    void setUserVisible(bool v);
    bool userVisible() const { return userVisible_; }
    void setMoveMode(bool on);
    bool moveMode() const { return moveMode_; }

signals:
    void offsetsChanged(int x, int y);
    void userVisibleChanged(bool visible);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    void refresh();
    void applyFlags();
    void reposition();
    QScreen* targetScreen() const;

    MetricsCollector* metrics_;
    Config cfg_;
    QImage image_;
    bool userVisible_ = true;
    bool moveMode_ = false;
    bool dragging_ = false;
    QPoint dragStartGlobal_;
    int dragStartOx_ = 0, dragStartOy_ = 0;
    QTimer raiseTimer_;
};
