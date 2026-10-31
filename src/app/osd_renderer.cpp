#include "osd_renderer.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>
#include <vector>

namespace {

enum class CellKind { Label, Value, Unit, Graph };

struct Cell {
    CellKind kind;
    QString text;
    const QFont* font = nullptr;
    QColor color;
    qreal width = 0;       // largura ocupada
    qreal textWidth = 0;   // largura real do texto (alinhamento à direita)
    qreal gapBefore = 0;   // espaço antes da célula
    const OsdItem* item = nullptr;
    qreal graphW = 0, graphH = 0;
};

struct Row {
    bool hasLabel = false;
    Cell label;
    std::vector<Cell> cells;
    qreal height = 0;
    qreal ascent = 0, descent = 0;
    qreal width = 0;
};

// Desfoque de caixa (2 passadas por eixo ~ gaussiano) em ARGB pré-multiplicado.
void boxBlur(QImage& img, int radius) {
    if (radius < 1) return;
    const int w = img.width(), h = img.height();
    std::vector<quint32> tmp(static_cast<size_t>(qMax(w, h)));
    auto pass = [&](bool horizontal) {
        const int len = horizontal ? w : h, lines = horizontal ? h : w;
        const int div = radius * 2 + 1;
        for (int l = 0; l < lines; ++l) {
            auto px = [&](int i) -> quint32& {
                int x = horizontal ? i : l, y = horizontal ? l : i;
                return reinterpret_cast<quint32*>(img.scanLine(y))[x];
            };
            int sum[4] = {0, 0, 0, 0};
            auto add = [&](quint32 c, int sign) {
                sum[0] += sign * static_cast<int>((c >> 24) & 0xff);
                sum[1] += sign * static_cast<int>((c >> 16) & 0xff);
                sum[2] += sign * static_cast<int>((c >> 8) & 0xff);
                sum[3] += sign * static_cast<int>(c & 0xff);
            };
            for (int i = -radius; i <= radius; ++i) add(px(qBound(0, i, len - 1)), 1);
            for (int i = 0; i < len; ++i) {
                tmp[static_cast<size_t>(i)] = (static_cast<quint32>(sum[0] / div) << 24) |
                                              (static_cast<quint32>(sum[1] / div) << 16) |
                                              (static_cast<quint32>(sum[2] / div) << 8) |
                                              static_cast<quint32>(sum[3] / div);
                add(px(qBound(0, i + radius + 1, len - 1)), 1);
                add(px(qBound(0, i - radius, len - 1)), -1);
            }
            for (int i = 0; i < len; ++i) px(i) = tmp[static_cast<size_t>(i)];
        }
    };
    for (int k = 0; k < 2; ++k) {
        pass(true);
        pass(false);
    }
}

void drawText(QPainter& p, const QPointF& baseline, const QString& text, const QFont& font, const QColor& color,
              const OsdStyle& st, qreal scale) {
    QPainterPath path;
    path.addText(baseline, font, text);
    if (st.outline && st.outlineWidth > 0) {
        QPen pen(st.outlineColor, st.outlineWidth * 2.0 * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.strokePath(path, pen);
    }
    p.fillPath(path, color);
}

void drawGraph(QPainter& p, const QRectF& r, const OsdItem& item, const QColor& color,
               const std::vector<float>& ft, const OsdStyle& st, qreal scale) {
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(item.graphBgColor);
    p.drawRoundedRect(r, 3 * scale, 3 * scale);

    if (ft.size() >= 2) {
        float mx = *std::max_element(ft.begin(), ft.end());
        double maxMs = item.graphMaxMs > 0 ? item.graphMaxMs : qMax(8.0, static_cast<double>(mx) * 1.25);
        const qreal inset = 2 * scale;
        QRectF g = r.adjusted(inset, inset, -inset, -inset);
        QPainterPath line;
        const size_t n = ft.size();
        for (size_t i = 0; i < n; ++i) {
            qreal x = g.left() + g.width() * static_cast<qreal>(i) / static_cast<qreal>(n - 1);
            qreal y = g.bottom() - g.height() * qMin(1.0, static_cast<double>(ft[i]) / maxMs);
            if (i == 0) line.moveTo(x, y);
            else line.lineTo(x, y);
        }
        if (item.graphFill) {
            QPainterPath area = line;
            area.lineTo(g.right(), g.bottom());
            area.lineTo(g.left(), g.bottom());
            area.closeSubpath();
            QColor fill = color;
            fill.setAlphaF(fill.alphaF() * 0.30);
            p.fillPath(area, fill);
        }
        if (st.outline && st.outlineWidth > 0) {
            p.strokePath(line, QPen(st.outlineColor, (item.graphLineWidth + st.outlineWidth) * scale, Qt::SolidLine,
                                    Qt::RoundCap, Qt::RoundJoin));
        }
        p.strokePath(line, QPen(color, item.graphLineWidth * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    p.restore();
}

}  // namespace

QImage OsdRenderer::render(const Config& cfg, const MetricsSnapshot& snap, qreal dpr) {
    const OsdStyle& st = cfg.style;
    const qreal scale = qBound(0.25, st.scale, 6.0);
    const QFont labelFont = st.labelFont.toFont(scale);
    const QFont valueFont = st.valueFont.toFont(scale);
    const QFont unitFont = st.unitFont.toFont(scale);
    const QFontMetricsF lfm(labelFont), vfm(valueFont), ufm(unitFont);
    const bool horizontal = st.layout == OsdLayout::Horizontal;
    const qreal itemGap = st.itemSpacing * scale;
    const qreal unitGap = 2 * scale;

    // ---------------------------------------------------------------- linhas
    std::vector<Row> rows;
    for (const OsdItem& item : cfg.items) {
        if (!item.enabled) continue;
        const MetricInfo& info = metricInfo(item.type);
        FormattedValue fv = formatMetric(item, snap, cfg.general);

        const QColor labelColor = item.customLabelColor ? item.labelColor : st.labelColor;
        QColor valueColor = item.customValueColor ? item.valueColor : st.valueColor;
        const QColor unitColor = item.customUnitColor ? item.unitColor : st.unitColor;
        if (item.alertEnabled && fv.valid && linosd::has(fv.numeric)) {
            bool trig = item.alertBelow ? fv.numeric < item.alertThreshold : fv.numeric > item.alertThreshold;
            if (trig) valueColor = item.alertColor;
        }

        bool startRow = rows.empty() || (!horizontal && item.newLine);
        if (startRow) rows.emplace_back();
        Row& row = rows.back();
        const bool hasLabel = item.showLabel && !item.label.isEmpty();
        const bool groupStart = horizontal && item.newLine && !row.cells.empty();

        bool firstInRow = row.cells.empty() && !row.hasLabel;
        if (hasLabel) {
            Cell c{CellKind::Label, item.label, &labelFont, labelColor};
            c.width = c.textWidth = lfm.horizontalAdvance(item.label);
            if (!horizontal && firstInRow) {
                row.hasLabel = true;
                row.label = c;
            } else {
                c.gapBefore = row.cells.empty() ? 0 : (groupStart ? itemGap * 1.8 : itemGap);
                row.cells.push_back(c);
            }
        }

        Cell v{CellKind::Value, fv.value, &valueFont, valueColor};
        v.item = &item;
        if (info.kind == MetricKind::Graph) {
            v.kind = CellKind::Graph;
            v.graphW = item.graphWidth * scale;
            v.graphH = item.graphHeight * scale;
            v.width = v.graphW;
            v.color = valueColor;
        } else {
            v.textWidth = vfm.horizontalAdvance(fv.value);
            v.width = v.textWidth;
            if (st.fixedValueWidth && info.kind == MetricKind::Number && info.widthTemplate[0])
                v.width = qMax(v.textWidth, vfm.horizontalAdvance(QString::fromLatin1(info.widthTemplate)));
        }
        if (row.cells.empty() || row.cells.back().kind == CellKind::Label)
            v.gapBefore = row.cells.empty() ? 0 : itemGap * 0.5;
        else
            v.gapBefore = groupStart ? itemGap * 1.8 : itemGap;
        row.cells.push_back(v);

        if (item.showUnit && !fv.unit.isEmpty() && info.kind == MetricKind::Number) {
            Cell u{CellKind::Unit, fv.unit, &unitFont, unitColor};
            u.width = u.textWidth = ufm.horizontalAdvance(fv.unit);
            u.gapBefore = unitGap;
            row.cells.push_back(u);
        }
    }

    // --------------------------------------------------------------- métricas
    qreal labelCol = 0;
    if (!horizontal && st.alignLabels)
        for (const Row& r : rows)
            if (r.hasLabel) labelCol = qMax(labelCol, r.label.width);

    qreal contentW = 0, contentH = 0;
    for (Row& r : rows) {
        qreal asc = 0, desc = 0, graphH = 0, x = 0;
        auto account = [&](const Cell& c) {
            if (c.kind == CellKind::Graph) {
                graphH = qMax(graphH, c.graphH);
                return;
            }
            QFontMetricsF fm(*c.font);
            asc = qMax(asc, fm.ascent());
            desc = qMax(desc, fm.descent());
        };
        if (r.hasLabel) {
            account(r.label);
            x = (st.alignLabels ? labelCol : r.label.width) + itemGap * 0.6;
        }
        for (const Cell& c : r.cells) {
            account(c);
            x += c.gapBefore + c.width;
        }
        r.ascent = asc;
        r.descent = desc;
        r.height = qMax(asc + desc, graphH);
        r.width = x;
        contentW = qMax(contentW, x);
        contentH += r.height;
    }
    if (rows.size() > 1) contentH += st.lineSpacing * scale * static_cast<qreal>(rows.size() - 1);

    // ------------------------------------------------------------- margens
    const qreal outlineM = st.outline ? st.outlineWidth * scale + 1 : 1;
    const qreal shadowM = st.shadow ? (qMax(qAbs(st.shadowX), qAbs(st.shadowY)) + st.shadowBlur * 2) * scale : 0;
    const qreal pad = (st.background ? st.padding * scale : 0) + outlineM + shadowM;
    const QSizeF logical(qMax<qreal>(1, contentW) + 2 * pad, qMax<qreal>(1, contentH) + 2 * pad);
    const QSize px(qCeil(logical.width() * dpr), qCeil(logical.height() * dpr));

    // ------------------------------------------------------- camada de texto
    QImage layer(px, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    {
        QPainter p(&layer);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.scale(dpr, dpr);
        qreal y = pad;
        for (const Row& r : rows) {
            const qreal textH = r.ascent + r.descent;
            const qreal baseline = y + (r.height - textH) / 2 + r.ascent;
            qreal x = pad;
            if (r.hasLabel) {
                drawText(p, QPointF(x, baseline), r.label.text, *r.label.font, r.label.color, st, scale);
                x += (st.alignLabels ? labelCol : r.label.width) + itemGap * 0.6;
            }
            for (const Cell& c : r.cells) {
                x += c.gapBefore;
                if (c.kind == CellKind::Graph) {
                    QRectF gr(x, y + (r.height - c.graphH) / 2, c.graphW, c.graphH);
                    drawGraph(p, gr, *c.item, c.color, snap.frames.frametimes, st, scale);
                } else {
                    // Valores numéricos alinhados à direita dentro da largura reservada.
                    qreal tx = (c.kind == CellKind::Value) ? x + (c.width - c.textWidth) : x;
                    drawText(p, QPointF(tx, baseline), c.text, *c.font, c.color, st, scale);
                }
                x += c.width;
            }
            y += r.height + st.lineSpacing * scale;
        }
    }

    // ------------------------------------------------------------ composição
    QImage out(px, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);  // coordenadas em pixels físicos
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(qBound(0.05, st.opacity, 1.0));

    if (st.background) {
        p.save();
        p.scale(dpr, dpr);
        qreal bw = st.border ? st.borderWidth * scale : 0;
        QRectF bg(bw / 2, bw / 2, logical.width() - bw, logical.height() - bw);
        p.setBrush(st.backgroundColor);
        if (st.border && bw > 0) p.setPen(QPen(st.borderColor, bw));
        else p.setPen(Qt::NoPen);
        p.drawRoundedRect(bg, st.cornerRadius * scale, st.cornerRadius * scale);
        p.restore();
    }

    if (st.shadow) {
        QImage shadow = layer;
        {
            QPainter sp(&shadow);
            sp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            sp.fillRect(shadow.rect(), st.shadowColor);
        }
        boxBlur(shadow, qRound(st.shadowBlur * scale * dpr * 0.6));
        p.drawImage(QPointF(st.shadowX * scale * dpr, st.shadowY * scale * dpr), shadow);
    }
    p.drawImage(QPointF(0, 0), layer);
    p.end();
    out.setDevicePixelRatio(dpr);
    return out;
}
