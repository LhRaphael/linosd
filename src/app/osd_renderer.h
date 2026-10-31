// Renderiza o OSD (texto com contorno/sombra, fundo e gráficos) em uma QImage.
#pragma once

#include <QImage>

#include "config.h"
#include "metrics.h"

namespace OsdRenderer {

// Retorna a imagem pronta (ARGB pré-multiplicado) com devicePixelRatio = dpr.
QImage render(const Config& cfg, const MetricsSnapshot& snap, qreal dpr = 1.0);

}  // namespace OsdRenderer
