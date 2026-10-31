#include "widgets.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

// ---------------------------------------------------------------------------
// ColorButton
// ---------------------------------------------------------------------------
ColorButton::ColorButton(QWidget* parent) : QToolButton(parent) {
    setMinimumSize(56, 24);
    setToolTip("Clique para escolher a cor (inclui transparência)");
    connect(this, &QToolButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(color_, this, "Escolher cor", QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            setColor(c);
            emit colorChanged(c);
        }
    });
}

void ColorButton::setColor(const QColor& c) {
    color_ = c;
    setToolTip(QString("%1 (opacidade %2%)").arg(c.name()).arg(qRound(c.alphaF() * 100)));
    update();
}

void ColorButton::paintEvent(QPaintEvent* e) {
    QToolButton::paintEvent(e);
    QPainter p(this);
    QRect r = rect().adjusted(5, 5, -5, -5);
    // Xadrez para visualizar a transparência
    const int s = 5;
    for (int y = r.top(); y < r.bottom(); y += s)
        for (int x = r.left(); x < r.right(); x += s)
            p.fillRect(QRect(x, y, s, s).intersected(r), ((x / s + y / s) % 2) ? QColor(200, 200, 200) : Qt::white);
    p.fillRect(r, color_);
    p.setPen(QColor(0, 0, 0, 120));
    p.drawRect(r);
}

// ---------------------------------------------------------------------------
// FontEditor
// ---------------------------------------------------------------------------
FontEditor::FontEditor(QWidget* parent) : QWidget(parent) {
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    family_ = new QFontComboBox(this);
    family_->setMinimumWidth(160);
    size_ = new QDoubleSpinBox(this);
    size_->setRange(4, 120);
    size_->setDecimals(1);
    size_->setSuffix(" pt");
    bold_ = new QToolButton(this);
    bold_->setText("N");
    bold_->setToolTip("Negrito");
    bold_->setCheckable(true);
    QFont bf = bold_->font();
    bf.setBold(true);
    bold_->setFont(bf);
    italic_ = new QToolButton(this);
    italic_->setText("I");
    italic_->setToolTip("Itálico");
    italic_->setCheckable(true);
    QFont itf = italic_->font();
    itf.setItalic(true);
    italic_->setFont(itf);
    l->addWidget(family_, 1);
    l->addWidget(size_);
    l->addWidget(bold_);
    l->addWidget(italic_);
    auto emitChanged = [this] {
        if (!blocking_) emit changed();
    };
    connect(family_, &QFontComboBox::currentFontChanged, this, emitChanged);
    connect(size_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, emitChanged);
    connect(bold_, &QToolButton::toggled, this, emitChanged);
    connect(italic_, &QToolButton::toggled, this, emitChanged);
}

FontSpec FontEditor::spec() const {
    FontSpec f;
    f.family = family_->currentFont().family();
    f.size = size_->value();
    f.bold = bold_->isChecked();
    f.italic = italic_->isChecked();
    return f;
}

void FontEditor::setSpec(const FontSpec& f) {
    blocking_ = true;
    family_->setCurrentFont(QFont(f.family));
    size_->setValue(f.size);
    bold_->setChecked(f.bold);
    italic_->setChecked(f.italic);
    blocking_ = false;
}

// ---------------------------------------------------------------------------
// PreviewWidget
// ---------------------------------------------------------------------------
PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(360, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PreviewWidget::setImage(const QImage& img, Anchor anchor) {
    image_ = img;
    anchor_ = anchor;
    update();
}

void PreviewWidget::setScene(int scene) {
    scene_ = scene;
    update();
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect();
    if (scene_ == 0) {
        // Cena escura: céu ao entardecer, montanhas e chão.
        QLinearGradient sky(r.topLeft(), r.bottomLeft());
        sky.setColorAt(0, QColor(25, 30, 60));
        sky.setColorAt(0.55, QColor(170, 90, 70));
        sky.setColorAt(1, QColor(40, 30, 30));
        p.fillRect(r, sky);
        QRadialGradient sun(r.width() * 0.7, r.height() * 0.55, r.height() * 0.25);
        sun.setColorAt(0, QColor(255, 220, 150, 230));
        sun.setColorAt(1, QColor(255, 220, 150, 0));
        p.fillRect(r, sun);
        QPainterPath m;
        m.moveTo(0, r.height() * 0.7);
        for (int i = 0; i <= 10; ++i)
            m.lineTo(r.width() * i / 10.0, r.height() * (0.55 + ((i * 37) % 7) * 0.03));
        m.lineTo(r.width(), r.height());
        m.lineTo(0, r.height());
        p.fillPath(m, QColor(20, 18, 28));
    } else if (scene_ == 1) {
        // Cena clara: neve / céu claro (teste de contraste).
        QLinearGradient sky(r.topLeft(), r.bottomLeft());
        sky.setColorAt(0, QColor(170, 210, 255));
        sky.setColorAt(1, QColor(250, 250, 255));
        p.fillRect(r, sky);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 220));
        p.drawEllipse(QRectF(r.width() * 0.05, r.height() * 0.15, 160, 50));
        p.drawEllipse(QRectF(r.width() * 0.55, r.height() * 0.08, 200, 60));
    } else {
        const int s = 12;
        for (int y = 0; y < height(); y += s)
            for (int x = 0; x < width(); x += s)
                p.fillRect(x, y, s, s, ((x / s + y / s) % 2) ? QColor(90, 90, 90) : QColor(140, 140, 140));
    }
    if (image_.isNull()) return;
    const qreal m = 12;
    QSizeF sz = QSizeF(image_.size()) / image_.devicePixelRatio();
    // OSD maior que a prévia (ex.: barra horizontal): reduz mantendo a proporção.
    const QSizeF avail(qMax<qreal>(10, r.width() - 2 * m), qMax<qreal>(10, r.height() - 2 * m));
    if (sz.width() > avail.width() || sz.height() > avail.height()) sz.scale(avail, Qt::KeepAspectRatio);
    const int a = static_cast<int>(anchor_);
    const int col = a % 3, row = a / 3;
    qreal x = col == 0 ? m : col == 1 ? (r.width() - sz.width()) / 2 : r.width() - sz.width() - m;
    qreal y = row == 0 ? m : row == 1 ? (r.height() - sz.height()) / 2 : r.height() - sz.height() - m;
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(QPointF(x, y), sz), image_);
}

// ---------------------------------------------------------------------------
QIcon makeAppIcon() {
    QIcon icon;
    for (int s : {16, 22, 32, 48, 64, 128, 256}) {
        QImage img(s, s, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, 0, s);
        g.setColorAt(0, QColor(40, 44, 60));
        g.setColorAt(1, QColor(15, 16, 24));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(0, 0, s, s), s * 0.2, s * 0.2);
        // Gráfico de frametime estilizado
        QPainterPath path;
        const qreal pts[] = {0.62, 0.55, 0.6, 0.35, 0.58, 0.6, 0.52, 0.57};
        for (int i = 0; i < 8; ++i) {
            qreal x = s * (0.15 + 0.7 * i / 7.0), y = s * pts[i];
            if (i == 0) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        p.setPen(QPen(QColor(255, 200, 60), qMax(1.5, s * 0.07), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(120, 220, 120));
        p.drawRoundedRect(QRectF(s * 0.15, s * 0.74, s * 0.45, s * 0.09), s * 0.04, s * 0.04);
        p.setBrush(QColor(90, 180, 255));
        p.drawRoundedRect(QRectF(s * 0.15, s * 0.16, s * 0.3, s * 0.09), s * 0.04, s * 0.04);
        p.end();
        icon.addPixmap(QPixmap::fromImage(img));
    }
    return icon;
}
