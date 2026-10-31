#include "overlay_window.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWindow>

#include "metrics.h"
#include "osd_renderer.h"

OverlayWindow::OverlayWindow(MetricsCollector* metrics, QWidget* parent) : QWidget(parent), metrics_(metrics) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_NoSystemBackground);
    setWindowTitle("LinOSD Overlay");
    applyFlags();
    connect(metrics_, &MetricsCollector::updated, this, &OverlayWindow::refresh);
    // Reafirma o empilhamento acima do jogo periodicamente.
    connect(&raiseTimer_, &QTimer::timeout, this, [this] {
        if (isVisible()) raise();
    });
    raiseTimer_.start(1500);
}

void OverlayWindow::applyFlags() {
    Qt::WindowFlags f = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus;
    if (!moveMode_) f |= Qt::WindowTransparentForInput | Qt::X11BypassWindowManagerHint;
    bool wasVisible = isVisible();
    setWindowFlags(f);
    setCursor(moveMode_ ? Qt::SizeAllCursor : Qt::ArrowCursor);
    if (wasVisible) show();
}

void OverlayWindow::setConfig(const Config& cfg) {
    cfg_ = cfg;
    refresh();
}

void OverlayWindow::setUserVisible(bool v) {
    if (userVisible_ == v) return;
    userVisible_ = v;
    emit userVisibleChanged(v);
    refresh();
}

void OverlayWindow::setMoveMode(bool on) {
    if (moveMode_ == on) return;
    moveMode_ = on;
    dragging_ = false;
    hide();
    applyFlags();
    refresh();
}

QScreen* OverlayWindow::targetScreen() const {
    if (!cfg_.position.screen.isEmpty())
        for (QScreen* s : QGuiApplication::screens())
            if (s->name() == cfg_.position.screen) return s;
    return QGuiApplication::primaryScreen();
}

void OverlayWindow::refresh() {
    MetricsSnapshot snap = metrics_->snapshot();
    const bool gameActive = snap.frames.active;
    bool show = userVisible_ && (moveMode_ || cfg_.general.displayMode == DisplayMode::Always || gameActive);
    if (!show) {
        if (isVisible()) hide();
        return;
    }
    if (!gameActive && moveMode_) {
        snap.frames = demoFrameStats(cfg_.general.graphFrames);
        snap.demoFrames = true;
    }
    QScreen* scr = targetScreen();
    qreal dpr = scr ? scr->devicePixelRatio() : 1.0;
    image_ = OsdRenderer::render(cfg_, snap, dpr);
    QSize logical = (QSizeF(image_.size()) / dpr).toSize();
    if (size() != logical) resize(logical);
    reposition();
    if (!isVisible()) {
        QWidget::show();
        raise();
    }
    update();
}

void OverlayWindow::reposition() {
    QScreen* scr = targetScreen();
    if (!scr) return;
    if (windowHandle() && windowHandle()->screen() != scr) windowHandle()->setScreen(scr);
    const QRect g = scr->geometry();
    const int a = static_cast<int>(cfg_.position.anchor);
    const int col = a % 3, row = a / 3;
    const int ox = cfg_.position.offsetX, oy = cfg_.position.offsetY;
    int x = col == 0 ? g.left() + ox : col == 1 ? g.center().x() - width() / 2 + ox : g.right() + 1 - width() - ox;
    int y = row == 0 ? g.top() + oy : row == 1 ? g.center().y() - height() / 2 + oy : g.bottom() + 1 - height() - oy;
    if (pos() != QPoint(x, y)) move(x, y);
}

void OverlayWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.drawImage(QPointF(0, 0), image_);
    if (moveMode_) {
        p.setPen(QPen(QColor(255, 200, 0), 2, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
}

void OverlayWindow::mousePressEvent(QMouseEvent* e) {
    if (!moveMode_ || e->button() != Qt::LeftButton) return;
    dragging_ = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    dragStartGlobal_ = e->globalPosition().toPoint();
#else
    dragStartGlobal_ = e->globalPos();
#endif
    dragStartOx_ = cfg_.position.offsetX;
    dragStartOy_ = cfg_.position.offsetY;
}

void OverlayWindow::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPoint d = e->globalPosition().toPoint() - dragStartGlobal_;
#else
    QPoint d = e->globalPos() - dragStartGlobal_;
#endif
    const int a = static_cast<int>(cfg_.position.anchor);
    const int col = a % 3, row = a / 3;
    cfg_.position.offsetX = dragStartOx_ + (col == 2 ? -d.x() : d.x());
    cfg_.position.offsetY = dragStartOy_ + (row == 2 ? -d.y() : d.y());
    reposition();
    emit offsetsChanged(cfg_.position.offsetX, cfg_.position.offsetY);
}

void OverlayWindow::mouseReleaseEvent(QMouseEvent*) { dragging_ = false; }
