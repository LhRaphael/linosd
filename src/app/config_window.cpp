#include "config_window.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "metrics.h"
#include "osd_renderer.h"
#include "overlay_window.h"
#include "widgets.h"

namespace {

QScrollArea* scrollWrap(QWidget* w) {
    auto* sa = new QScrollArea;
    sa->setWidget(w);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    return sa;
}

QSpinBox* spin(int min, int max, const QString& suffix = QString()) {
    auto* s = new QSpinBox;
    s->setRange(min, max);
    s->setSuffix(suffix);
    return s;
}

QDoubleSpinBox* dspin(double min, double max, double step, int decimals, const QString& suffix = QString()) {
    auto* s = new QDoubleSpinBox;
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setSuffix(suffix);
    return s;
}

QWidget* row(std::initializer_list<QWidget*> ws) {
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    for (QWidget* x : ws) l->addWidget(x);
    l->addStretch();
    return w;
}

}  // namespace

ConfigWindow::ConfigWindow(const Config& cfg, MetricsCollector* metrics, OverlayWindow* overlay, QWidget* parent)
    : QMainWindow(parent), cfg_(cfg), metrics_(metrics), overlay_(overlay) {
    setWindowTitle("Monitor de desempenho para jogos");
    setWindowIcon(makeAppIcon());
    resize(1180, 760);

    // Barra superior: presets e arquivos
    auto* top = new QWidget;
    auto* topL = new QHBoxLayout(top);
    topL->setContentsMargins(8, 6, 8, 0);
    preset_ = new QComboBox;
    preset_->addItems(Config::presetNames());
    auto* applyPreset = new QPushButton("Aplicar preset");
    auto* importBtn = new QPushButton("Importar…");
    auto* exportBtn = new QPushButton("Exportar…");
    auto* resetBtn = new QPushButton("Restaurar padrão");
    topL->addWidget(new QLabel("Preset:"));
    topL->addWidget(preset_);
    topL->addWidget(applyPreset);
    topL->addStretch();
    topL->addWidget(importBtn);
    topL->addWidget(exportBtn);
    topL->addWidget(resetBtn);

    auto* tabs = new QTabWidget;
    tabs->addTab(buildItemsTab(), "Itens / Métricas");
    tabs->addTab(scrollWrap(buildAppearanceTab()), "Aparência");
    tabs->addTab(scrollWrap(buildPositionTab()), "Posição");
    tabs->addTab(scrollWrap(buildGeneralTab()), "Geral");

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(tabs);
    split->addWidget(buildPreviewPanel());
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    auto* central = new QWidget;
    auto* cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(top);
    cl->addWidget(split, 1);
    setCentralWidget(central);

    status_ = new QLabel;
    statusBar()->addWidget(status_, 1);

    connect(applyPreset, &QPushButton::clicked, this, [this] {
        cfg_ = Config::preset(preset_->currentText(), cfg_);
        loadToUi();
        changed();
    });
    connect(resetBtn, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, "Restaurar padrão", "Descartar todas as personalizações?") != QMessageBox::Yes)
            return;
        cfg_ = Config::defaults();
        loadToUi();
        changed();
    });
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        QString path = QFileDialog::getSaveFileName(this, "Exportar configuração", QDir::homePath() + "/linosd-perfil.json",
                                                    "JSON (*.json)");
        if (!path.isEmpty() && !cfg_.saveTo(path)) QMessageBox::warning(this, "Erro", "Não foi possível salvar o arquivo.");
    });
    connect(importBtn, &QPushButton::clicked, this, [this] {
        QString path = QFileDialog::getOpenFileName(this, "Importar configuração", QDir::homePath(), "JSON (*.json)");
        if (path.isEmpty()) return;
        Config c;
        if (!Config::loadFrom(path, c)) {
            QMessageBox::warning(this, "Erro", "Arquivo inválido.");
            return;
        }
        cfg_ = c;
        loadToUi();
        changed();
    });

    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(400);
    connect(&saveTimer_, &QTimer::timeout, this, [this] {
        if (!cfg_.save()) statusBar()->showMessage("Falha ao salvar " + Config::configPath(), 4000);
    });

    connect(metrics_, &MetricsCollector::updated, this, [this] {
        if (!isVisible()) return;
        updatePreview();
        updateStatus();
    });
    connect(overlay_, &OverlayWindow::offsetsChanged, this, [this](int x, int y) {
        loading_ = true;
        offsetX_->setValue(x);
        offsetY_->setValue(y);
        loading_ = false;
        cfg_.position.offsetX = x;
        cfg_.position.offsetY = y;
        saveTimer_.start();
    });
    connect(overlay_, &OverlayWindow::userVisibleChanged, this, [this](bool v) {
        loading_ = true;
        overlayVisible_->setChecked(v);
        loading_ = false;
    });

    loadToUi();
    updatePreview();
    updateStatus();
}

// ===========================================================================
// Aba: itens
// ===========================================================================
QWidget* ConfigWindow::buildItemsTab() {
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);

    // Lista
    auto* left = new QVBoxLayout;
    itemList_ = new QListWidget;
    itemList_->setDragDropMode(QAbstractItemView::InternalMove);
    itemList_->setDefaultDropAction(Qt::MoveAction);
    itemList_->setMinimumWidth(250);
    left->addWidget(new QLabel("Itens do OSD (arraste para reordenar, marque para exibir):"));
    left->addWidget(itemList_, 1);

    auto* addBtn = new QToolButton;
    addBtn->setText("Adicionar");
    addBtn->setPopupMode(QToolButton::InstantPopup);
    addBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* addMenu = new QMenu(addBtn);
    QMap<QString, QMenu*> cats;
    for (int i = 0; i < static_cast<int>(MetricType::Count); ++i) {
        const MetricInfo& mi = metricInfo(static_cast<MetricType>(i));
        QString cat = QString::fromUtf8(mi.category);
        if (!cats.contains(cat)) cats[cat] = addMenu->addMenu(cat);
        QAction* a = cats[cat]->addAction(QString::fromUtf8(mi.displayName));
        connect(a, &QAction::triggered, this, [this, i] { addItem(static_cast<MetricType>(i)); });
    }
    addBtn->setMenu(addMenu);
    auto* removeBtn = new QPushButton("Remover");
    auto* dupBtn = new QPushButton("Duplicar");
    auto* upBtn = new QPushButton("▲");
    auto* downBtn = new QPushButton("▼");
    upBtn->setToolTip("Mover para cima");
    downBtn->setToolTip("Mover para baixo");
    upBtn->setFixedWidth(36);
    downBtn->setFixedWidth(36);
    auto* btns = new QHBoxLayout;
    btns->addWidget(addBtn);
    btns->addWidget(removeBtn);
    btns->addWidget(dupBtn);
    btns->addStretch();
    btns->addWidget(upBtn);
    btns->addWidget(downBtn);
    left->addLayout(btns);
    l->addLayout(left, 2);

    // Editor do item
    itemEditor_ = new QWidget;
    auto* ed = new QVBoxLayout(itemEditor_);
    itemMetric_ = new QLabel;
    QFont mf = itemMetric_->font();
    mf.setBold(true);
    mf.setPointSizeF(mf.pointSizeF() * 1.15);
    itemMetric_->setFont(mf);
    ed->addWidget(itemMetric_);

    auto* basic = new QGroupBox("Texto e disposição");
    auto* bf = new QFormLayout(basic);
    itemLabel_ = new QLineEdit;
    itemLabel_->setPlaceholderText("ex.: GPU, CPU, FPS…");
    itemShowLabel_ = new QCheckBox("Mostrar rótulo");
    itemNewLine_ = new QCheckBox("Iniciar nova linha");
    itemNewLine_->setToolTip("Desmarque para exibir este item na mesma linha do item anterior "
                             "(ex.: GPU 45% 65°C 1800MHz)");
    itemShowUnit_ = new QCheckBox("Mostrar unidade");
    itemDecimals_ = spin(-1, 4);
    itemDecimals_->setSpecialValueText("Padrão");
    bf->addRow("Rótulo:", itemLabel_);
    bf->addRow("", itemShowLabel_);
    bf->addRow("", itemNewLine_);
    bf->addRow("", itemShowUnit_);
    bf->addRow("Casas decimais:", itemDecimals_);
    ed->addWidget(basic);

    auto* colors = new QGroupBox("Cores deste item (desmarcado = usa a cor global)");
    auto* cf = new QFormLayout(colors);
    itemCustomLabelColor_ = new QCheckBox("Personalizar");
    itemLabelColor_ = new ColorButton;
    itemCustomValueColor_ = new QCheckBox("Personalizar");
    itemValueColor_ = new ColorButton;
    itemCustomUnitColor_ = new QCheckBox("Personalizar");
    itemUnitColor_ = new ColorButton;
    cf->addRow("Rótulo:", row({itemCustomLabelColor_, itemLabelColor_}));
    cf->addRow("Valor / gráfico:", row({itemCustomValueColor_, itemValueColor_}));
    cf->addRow("Unidade:", row({itemCustomUnitColor_, itemUnitColor_}));
    ed->addWidget(colors);

    alertGroup_ = new QGroupBox("Alerta de cor (muda a cor do valor ao passar do limite)");
    alertGroup_->setCheckable(true);
    auto* af = new QFormLayout(alertGroup_);
    alertCond_ = new QComboBox;
    alertCond_->addItems({"Acima de", "Abaixo de"});
    alertThreshold_ = dspin(-100000, 100000, 1, 1);
    alertColor_ = new ColorButton;
    af->addRow("Condição:", row({alertCond_, alertThreshold_}));
    af->addRow("Cor de alerta:", alertColor_);
    ed->addWidget(alertGroup_);

    graphGroup_ = new QGroupBox("Gráfico de frametime");
    auto* gf = new QFormLayout(graphGroup_);
    graphW_ = spin(40, 2000, " px");
    graphH_ = spin(10, 600, " px");
    graphMax_ = dspin(0, 1000, 1, 1, " ms");
    graphMax_->setSpecialValueText("Automático");
    graphLine_ = dspin(0.5, 10, 0.5, 1, " px");
    graphFill_ = new QCheckBox("Preencher área sob a linha");
    graphBg_ = new ColorButton;
    gf->addRow("Largura:", graphW_);
    gf->addRow("Altura:", graphH_);
    gf->addRow("Escala máxima:", graphMax_);
    gf->addRow("Espessura da linha:", graphLine_);
    gf->addRow("", graphFill_);
    gf->addRow("Fundo do gráfico:", graphBg_);
    ed->addWidget(graphGroup_);

    textGroup_ = new QGroupBox("Texto");
    auto* tf = new QVBoxLayout(textGroup_);
    itemText_ = new QLineEdit;
    itemTextHelp_ = new QLabel;
    itemTextHelp_->setWordWrap(true);
    itemTextHelp_->setStyleSheet("color: gray;");
    tf->addWidget(itemText_);
    tf->addWidget(itemTextHelp_);
    ed->addWidget(textGroup_);
    ed->addStretch();
    l->addWidget(scrollWrap(itemEditor_), 3);

    // Sinais
    connect(itemList_, &QListWidget::currentRowChanged, this, [this] { loadItemEditor(); });
    connect(itemList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* li) {
        if (loading_) return;
        int idx = itemList_->row(li);
        if (idx < 0 || idx >= cfg_.items.size()) return;
        cfg_.items[idx].enabled = li->checkState() == Qt::Checked;
        changed();
    });
    connect(itemList_->model(), &QAbstractItemModel::rowsMoved, this, [this] { onRowsMoved(); });
    connect(removeBtn, &QPushButton::clicked, this, [this] {
        int r = itemList_->currentRow();
        if (r < 0 || r >= cfg_.items.size()) return;
        cfg_.items.removeAt(r);
        refreshItemList(qMin(r, cfg_.items.size() - 1));
        changed();
    });
    connect(dupBtn, &QPushButton::clicked, this, [this] {
        int r = itemList_->currentRow();
        if (r < 0 || r >= cfg_.items.size()) return;
        cfg_.items.insert(r + 1, cfg_.items[r]);
        refreshItemList(r + 1);
        changed();
    });
    connect(upBtn, &QPushButton::clicked, this, [this] { moveItem(-1); });
    connect(downBtn, &QPushButton::clicked, this, [this] { moveItem(1); });

    auto onEdit = [this] {
        if (!loading_) readItemEditor();
    };
    for (QLineEdit* e : {itemLabel_, itemText_}) connect(e, &QLineEdit::textChanged, this, onEdit);
    for (QCheckBox* c : {itemShowLabel_, itemNewLine_, itemShowUnit_, itemCustomLabelColor_, itemCustomValueColor_,
                         itemCustomUnitColor_, graphFill_})
        connect(c, &QCheckBox::toggled, this, onEdit);
    for (QSpinBox* s : {itemDecimals_, graphW_, graphH_}) connect(s, qOverload<int>(&QSpinBox::valueChanged), this, onEdit);
    for (QDoubleSpinBox* s : {alertThreshold_, graphMax_, graphLine_})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, onEdit);
    for (ColorButton* b : {itemLabelColor_, itemValueColor_, itemUnitColor_, alertColor_, graphBg_})
        connect(b, &ColorButton::colorChanged, this, onEdit);
    connect(alertGroup_, &QGroupBox::toggled, this, onEdit);
    connect(alertCond_, qOverload<int>(&QComboBox::currentIndexChanged), this, onEdit);
    return w;
}

QString ConfigWindow::itemText(const OsdItem& it) const {
    const MetricInfo& mi = metricInfo(it.type);
    QString name = QString::fromUtf8(mi.displayName);
    QString lbl = (it.showLabel && !it.label.isEmpty()) ? QString("[%1] ").arg(it.label) : QString();
    if (it.type == MetricType::CustomText && !it.text.isEmpty()) name += QString(" “%1”").arg(it.text);
    bool horizontal = cfg_.style.layout == OsdLayout::Horizontal;
    QString prefix = (!it.newLine && !horizontal) ? QStringLiteral("      ↳ ") : QString();
    return prefix + lbl + name;
}

void ConfigWindow::refreshItemList(int select) {
    loading_ = true;
    int cur = select >= 0 ? select : itemList_->currentRow();
    itemList_->clear();
    for (const OsdItem& it : cfg_.items) {
        auto* li = new QListWidgetItem(itemText(it));
        li->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
        li->setCheckState(it.enabled ? Qt::Checked : Qt::Unchecked);
        itemList_->addItem(li);
    }
    loading_ = false;
    if (cfg_.items.isEmpty()) {
        itemEditor_->setEnabled(false);
        return;
    }
    itemList_->setCurrentRow(qBound(0, cur, cfg_.items.size() - 1));
    loadItemEditor();
}

void ConfigWindow::onRowsMoved() {
    if (loading_) return;
    // Reconstrói a ordem a partir do texto/estado atual da lista: usamos o
    // índice original guardado em Qt::UserRole.
    QVector<OsdItem> reordered;
    for (int i = 0; i < itemList_->count(); ++i) {
        int orig = itemList_->item(i)->data(Qt::UserRole).toInt();
        if (orig >= 0 && orig < cfg_.items.size()) reordered.append(cfg_.items[orig]);
    }
    if (reordered.size() == cfg_.items.size()) cfg_.items = reordered;
    int cur = itemList_->currentRow();
    QTimer::singleShot(0, this, [this, cur] { refreshItemList(cur); });
    changed();
}

void ConfigWindow::moveItem(int delta) {
    int r = itemList_->currentRow();
    int to = r + delta;
    if (r < 0 || to < 0 || to >= cfg_.items.size()) return;
    cfg_.items.swapItemsAt(r, to);
    refreshItemList(to);
    changed();
}

void ConfigWindow::addItem(MetricType t) {
    int r = itemList_->currentRow();
    int at = (r >= 0) ? r + 1 : cfg_.items.size();
    cfg_.items.insert(at, OsdItem::make(t));
    refreshItemList(at);
    changed();
}

void ConfigWindow::loadItemEditor() {
    // Atualiza os índices originais (para reordenação por arrastar).
    for (int i = 0; i < itemList_->count(); ++i) itemList_->item(i)->setData(Qt::UserRole, i);
    int r = itemList_->currentRow();
    bool valid = r >= 0 && r < cfg_.items.size();
    itemEditor_->setEnabled(valid);
    if (!valid) return;
    const OsdItem& it = cfg_.items[r];
    const MetricInfo& mi = metricInfo(it.type);
    loading_ = true;
    itemMetric_->setText(QString::fromUtf8(mi.displayName) + "  ·  " + QString::fromUtf8(mi.category));
    itemLabel_->setText(it.label);
    itemShowLabel_->setChecked(it.showLabel);
    itemNewLine_->setChecked(it.newLine);
    itemShowUnit_->setChecked(it.showUnit);
    itemDecimals_->setValue(it.decimals);
    itemCustomLabelColor_->setChecked(it.customLabelColor);
    itemLabelColor_->setColor(it.customLabelColor ? it.labelColor : cfg_.style.labelColor);
    itemCustomValueColor_->setChecked(it.customValueColor);
    itemValueColor_->setColor(it.customValueColor ? it.valueColor : cfg_.style.valueColor);
    itemCustomUnitColor_->setChecked(it.customUnitColor);
    itemUnitColor_->setColor(it.customUnitColor ? it.unitColor : cfg_.style.unitColor);
    alertGroup_->setChecked(it.alertEnabled);
    alertCond_->setCurrentIndex(it.alertBelow ? 1 : 0);
    alertThreshold_->setValue(it.alertThreshold);
    alertColor_->setColor(it.alertColor);
    graphW_->setValue(it.graphWidth);
    graphH_->setValue(it.graphHeight);
    graphMax_->setValue(it.graphMaxMs);
    graphLine_->setValue(it.graphLineWidth);
    graphFill_->setChecked(it.graphFill);
    graphBg_->setColor(it.graphBgColor);
    itemText_->setText(it.text);

    const bool isNumber = mi.kind == MetricKind::Number;
    const bool isGraph = mi.kind == MetricKind::Graph;
    const bool hasText = it.type == MetricType::Clock || it.type == MetricType::CustomText;
    itemShowUnit_->setVisible(isNumber);
    itemDecimals_->setEnabled(isNumber);
    alertGroup_->setVisible(isNumber);
    graphGroup_->setVisible(isGraph);
    textGroup_->setVisible(hasText);
    itemUnitColor_->setEnabled(isNumber);
    itemCustomUnitColor_->setEnabled(isNumber);
    if (it.type == MetricType::Clock)
        itemTextHelp_->setText("Formato do relógio: HH (hora), mm (minuto), ss (segundo), dd/MM/yyyy (data), "
                               "AP (AM/PM). Ex.: HH:mm  ·  dd/MM HH:mm:ss");
    else
        itemTextHelp_->setText("Texto livre exibido no OSD (ex.: nome do perfil, seu canal…).");
    loading_ = false;
}

void ConfigWindow::readItemEditor() {
    int r = itemList_->currentRow();
    if (r < 0 || r >= cfg_.items.size()) return;
    OsdItem& it = cfg_.items[r];
    it.label = itemLabel_->text();
    it.showLabel = itemShowLabel_->isChecked();
    it.newLine = itemNewLine_->isChecked();
    it.showUnit = itemShowUnit_->isChecked();
    it.decimals = itemDecimals_->value();
    it.customLabelColor = itemCustomLabelColor_->isChecked();
    it.customValueColor = itemCustomValueColor_->isChecked();
    it.customUnitColor = itemCustomUnitColor_->isChecked();
    // Clicar em uma cor ativa a personalização automaticamente.
    if (sender() == itemLabelColor_) it.customLabelColor = true;
    if (sender() == itemValueColor_) it.customValueColor = true;
    if (sender() == itemUnitColor_) it.customUnitColor = true;
    it.labelColor = itemLabelColor_->color();
    it.valueColor = itemValueColor_->color();
    it.unitColor = itemUnitColor_->color();
    it.alertEnabled = alertGroup_->isChecked();
    it.alertBelow = alertCond_->currentIndex() == 1;
    it.alertThreshold = alertThreshold_->value();
    it.alertColor = alertColor_->color();
    it.graphWidth = graphW_->value();
    it.graphHeight = graphH_->value();
    it.graphMaxMs = graphMax_->value();
    it.graphLineWidth = graphLine_->value();
    it.graphFill = graphFill_->isChecked();
    it.graphBgColor = graphBg_->color();
    it.text = itemText_->text();

    loading_ = true;
    itemCustomLabelColor_->setChecked(it.customLabelColor);
    itemCustomValueColor_->setChecked(it.customValueColor);
    itemCustomUnitColor_->setChecked(it.customUnitColor);
    if (QListWidgetItem* li = itemList_->item(r)) li->setText(itemText(it));
    loading_ = false;
    changed();
}

// ===========================================================================
// Aba: aparência
// ===========================================================================
QWidget* ConfigWindow::buildAppearanceTab() {
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);

    auto* fonts = new QGroupBox("Fontes");
    auto* ff = new QFormLayout(fonts);
    labelFont_ = new FontEditor;
    valueFont_ = new FontEditor;
    unitFont_ = new FontEditor;
    ff->addRow("Rótulos:", labelFont_);
    ff->addRow("Valores:", valueFont_);
    ff->addRow("Unidades:", unitFont_);
    scale_ = spin(25, 600, " %");
    scale_->setSingleStep(10);
    scale_->setToolTip("Escala geral do OSD (útil para monitores 1440p/4K)");
    ff->addRow("Escala geral:", scale_);
    l->addWidget(fonts);

    auto* colors = new QGroupBox("Cores globais");
    auto* cf = new QFormLayout(colors);
    labelColor_ = new ColorButton;
    valueColor_ = new ColorButton;
    unitColor_ = new ColorButton;
    cf->addRow("Rótulos:", labelColor_);
    cf->addRow("Valores:", valueColor_);
    cf->addRow("Unidades:", unitColor_);
    auto* clearCustom = new QPushButton("Remover cores personalizadas de todos os itens");
    cf->addRow("", clearCustom);
    l->addWidget(colors);

    outlineGroup_ = new QGroupBox("Contorno do texto");
    outlineGroup_->setCheckable(true);
    auto* of = new QFormLayout(outlineGroup_);
    outlineColor_ = new ColorButton;
    outlineWidth_ = dspin(0.25, 8, 0.25, 2, " px");
    of->addRow("Cor:", outlineColor_);
    of->addRow("Espessura:", outlineWidth_);
    l->addWidget(outlineGroup_);

    shadowGroup_ = new QGroupBox("Sombra");
    shadowGroup_->setCheckable(true);
    auto* sf = new QFormLayout(shadowGroup_);
    shadowColor_ = new ColorButton;
    shadowX_ = spin(-30, 30, " px");
    shadowY_ = spin(-30, 30, " px");
    shadowBlur_ = spin(0, 30, " px");
    sf->addRow("Cor:", shadowColor_);
    sf->addRow("Deslocamento X:", shadowX_);
    sf->addRow("Deslocamento Y:", shadowY_);
    sf->addRow("Desfoque:", shadowBlur_);
    l->addWidget(shadowGroup_);

    bgGroup_ = new QGroupBox("Plano de fundo");
    bgGroup_->setCheckable(true);
    auto* bgf = new QFormLayout(bgGroup_);
    bgColor_ = new ColorButton;
    bgRadius_ = spin(0, 60, " px");
    bgPadding_ = spin(0, 80, " px");
    borderEnabled_ = new QCheckBox("Borda");
    borderColor_ = new ColorButton;
    borderWidth_ = dspin(0.5, 10, 0.5, 1, " px");
    bgf->addRow("Cor (com transparência):", bgColor_);
    bgf->addRow("Arredondamento dos cantos:", bgRadius_);
    bgf->addRow("Margem interna:", bgPadding_);
    bgf->addRow(borderEnabled_, row({borderColor_, borderWidth_}));
    l->addWidget(bgGroup_);

    auto* lay = new QGroupBox("Layout e espaçamento");
    auto* lf = new QFormLayout(lay);
    layout_ = new QComboBox;
    layout_->addItems({"Vertical (uma linha por grupo)", "Horizontal (barra única)"});
    lineSpacing_ = spin(-10, 60, " px");
    itemSpacing_ = spin(0, 100, " px");
    alignLabels_ = new QCheckBox("Alinhar rótulos em coluna");
    fixedWidth_ = new QCheckBox("Largura fixa dos valores (evita que o OSD “trema”)");
    opacity_ = new QSlider(Qt::Horizontal);
    opacity_->setRange(5, 100);
    opacityLabel_ = new QLabel;
    lf->addRow("Disposição:", layout_);
    lf->addRow("Espaço entre linhas:", lineSpacing_);
    lf->addRow("Espaço entre itens:", itemSpacing_);
    lf->addRow("", alignLabels_);
    lf->addRow("", fixedWidth_);
    auto* opRow = new QWidget;
    auto* opl = new QHBoxLayout(opRow);
    opl->setContentsMargins(0, 0, 0, 0);
    opl->addWidget(opacity_, 1);
    opl->addWidget(opacityLabel_);
    lf->addRow("Opacidade geral:", opRow);
    l->addWidget(lay);
    l->addStretch();

    auto onEdit = [this] {
        if (!loading_) readStyle();
    };
    for (FontEditor* f : {labelFont_, valueFont_, unitFont_}) connect(f, &FontEditor::changed, this, onEdit);
    for (ColorButton* b : {labelColor_, valueColor_, unitColor_, outlineColor_, shadowColor_, bgColor_, borderColor_})
        connect(b, &ColorButton::colorChanged, this, onEdit);
    for (QGroupBox* g : {outlineGroup_, shadowGroup_, bgGroup_}) connect(g, &QGroupBox::toggled, this, onEdit);
    for (QSpinBox* s : {shadowX_, shadowY_, shadowBlur_, bgRadius_, bgPadding_, lineSpacing_, itemSpacing_, scale_})
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, onEdit);
    for (QDoubleSpinBox* s : {outlineWidth_, borderWidth_})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, onEdit);
    for (QCheckBox* c : {borderEnabled_, alignLabels_, fixedWidth_}) connect(c, &QCheckBox::toggled, this, onEdit);
    connect(layout_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (loading_) return;
        readStyle();
        refreshItemList();
    });
    connect(opacity_, &QSlider::valueChanged, this, onEdit);
    connect(clearCustom, &QPushButton::clicked, this, [this] {
        for (auto& it : cfg_.items) it.customLabelColor = it.customValueColor = it.customUnitColor = false;
        loadItemEditor();
        changed();
    });
    return w;
}

void ConfigWindow::readStyle() {
    OsdStyle& s = cfg_.style;
    s.labelFont = labelFont_->spec();
    s.valueFont = valueFont_->spec();
    s.unitFont = unitFont_->spec();
    s.scale = scale_->value() / 100.0;
    s.labelColor = labelColor_->color();
    s.valueColor = valueColor_->color();
    s.unitColor = unitColor_->color();
    s.outline = outlineGroup_->isChecked();
    s.outlineColor = outlineColor_->color();
    s.outlineWidth = outlineWidth_->value();
    s.shadow = shadowGroup_->isChecked();
    s.shadowColor = shadowColor_->color();
    s.shadowX = shadowX_->value();
    s.shadowY = shadowY_->value();
    s.shadowBlur = shadowBlur_->value();
    s.background = bgGroup_->isChecked();
    s.backgroundColor = bgColor_->color();
    s.cornerRadius = bgRadius_->value();
    s.padding = bgPadding_->value();
    s.border = borderEnabled_->isChecked();
    s.borderColor = borderColor_->color();
    s.borderWidth = borderWidth_->value();
    s.layout = static_cast<OsdLayout>(layout_->currentIndex());
    s.lineSpacing = lineSpacing_->value();
    s.itemSpacing = itemSpacing_->value();
    s.alignLabels = alignLabels_->isChecked();
    s.fixedValueWidth = fixedWidth_->isChecked();
    s.opacity = opacity_->value() / 100.0;
    opacityLabel_->setText(QString("%1%").arg(opacity_->value()));
    changed();
}

// ===========================================================================
// Aba: posição
// ===========================================================================
QWidget* ConfigWindow::buildPositionTab() {
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);

    auto* scr = new QGroupBox("Monitor");
    auto* sf = new QFormLayout(scr);
    screen_ = new QComboBox;
    screen_->addItem("Monitor principal", QString());
    for (QScreen* s : QGuiApplication::screens())
        screen_->addItem(QString("%1 (%2x%3)").arg(s->name()).arg(s->geometry().width()).arg(s->geometry().height()),
                         s->name());
    sf->addRow("Exibir em:", screen_);
    l->addWidget(scr);

    auto* anc = new QGroupBox("Ponto de ancoragem na tela");
    auto* grid = new QGridLayout(anc);
    anchorGroup_ = new QButtonGroup(this);
    anchorGroup_->setExclusive(true);
    const char* names[] = {"↖ Superior esquerdo", "↑ Superior centro", "↗ Superior direito",
                           "← Centro esquerdo",  "• Centro",          "→ Centro direito",
                           "↙ Inferior esquerdo", "↓ Inferior centro", "↘ Inferior direito"};
    for (int i = 0; i < 9; ++i) {
        auto* b = new QPushButton(QString::fromUtf8(names[i]));
        b->setCheckable(true);
        b->setMinimumHeight(42);
        anchorGroup_->addButton(b, i);
        grid->addWidget(b, i / 3, i % 3);
    }
    l->addWidget(anc);

    auto* off = new QGroupBox("Deslocamento a partir da âncora");
    auto* of = new QFormLayout(off);
    offsetX_ = spin(-10000, 10000, " px");
    offsetY_ = spin(-10000, 10000, " px");
    of->addRow("Horizontal (X):", offsetX_);
    of->addRow("Vertical (Y):", offsetY_);
    auto* zero = new QPushButton("Zerar deslocamento");
    moveMode_ = new QPushButton("Posicionar com o mouse");
    moveMode_->setCheckable(true);
    moveMode_->setToolTip("Torna o overlay arrastável na tela. Desative ao terminar.");
    of->addRow("", row({zero, moveMode_}));
    auto* hint = new QLabel("Nas âncoras à direita/inferiores o deslocamento é medido a partir da borda "
                            "direita/inferior. Use “Posicionar com o mouse” para arrastar o overlay diretamente na tela.");
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray;");
    of->addRow(hint);
    l->addWidget(off);
    l->addStretch();

    auto onEdit = [this] {
        if (!loading_) readPosition();
    };
    connect(screen_, qOverload<int>(&QComboBox::currentIndexChanged), this, onEdit);
    connect(anchorGroup_, &QButtonGroup::idClicked, this, onEdit);
    connect(offsetX_, qOverload<int>(&QSpinBox::valueChanged), this, onEdit);
    connect(offsetY_, qOverload<int>(&QSpinBox::valueChanged), this, onEdit);
    connect(zero, &QPushButton::clicked, this, [this] {
        offsetX_->setValue(0);
        offsetY_->setValue(0);
    });
    connect(moveMode_, &QPushButton::toggled, this, [this](bool on) {
        overlay_->setMoveMode(on);
        moveMode_->setText(on ? "Concluir posicionamento" : "Posicionar com o mouse");
    });
    return w;
}

void ConfigWindow::readPosition() {
    cfg_.position.screen = screen_->currentData().toString();
    cfg_.position.anchor = static_cast<Anchor>(qMax(0, anchorGroup_->checkedId()));
    cfg_.position.offsetX = offsetX_->value();
    cfg_.position.offsetY = offsetY_->value();
    changed();
}

// ===========================================================================
// Aba: geral
// ===========================================================================
QWidget* ConfigWindow::buildGeneralTab() {
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);

    auto* disp = new QGroupBox("Exibição");
    auto* df = new QFormLayout(disp);
    displayMode_ = new QComboBox;
    displayMode_->addItems({"Sempre visível", "Somente com jogo detectado"});
    refreshMs_ = spin(16, 2000, " ms");
    sensorMs_ = spin(100, 10000, " ms");
    df->addRow("Mostrar overlay:", displayMode_);
    df->addRow("Atualização do overlay:", refreshMs_);
    df->addRow("Leitura dos sensores:", sensorMs_);
    l->addWidget(disp);

    auto* fr = new QGroupBox("Frames");
    auto* ff = new QFormLayout(fr);
    fpsWindow_ = spin(100, 5000, " ms");
    fpsWindow_->setToolTip("Período usado para a média do FPS e do frametime");
    lowsWindow_ = spin(1, 15, " s");
    lowsWindow_->setToolTip("Período usado para calcular 1% low, 0.1% low, mínimo e máximo");
    lowsMethod_ = new QComboBox;
    lowsMethod_->addItems({"Percentil (como Afterburner/RTSS)", "Média dos piores frames (como CapFrameX)"});
    graphFrames_ = spin(10, 2000, " frames");
    fpsLimit_ = spin(0, 1000, " FPS");
    fpsLimit_->setSpecialValueText("Sem limite");
    ff->addRow("Janela do FPS médio:", fpsWindow_);
    ff->addRow("Janela dos lows:", lowsWindow_);
    ff->addRow("Cálculo dos lows:", lowsMethod_);
    ff->addRow("Frames no gráfico:", graphFrames_);
    ff->addRow("Limitador de FPS:", fpsLimit_);
    l->addWidget(fr);

    auto* hw = new QGroupBox("Hardware e unidades");
    auto* hf = new QFormLayout(hw);
    gpu_ = new QComboBox;
    gpu_->addItem("Automático (prefere a dedicada)", QString());
    for (const auto& g : metrics_->gpus())
        gpu_->addItem(QString::fromStdString(g.vendor + " — " + g.name + "  (" + g.id + ")"), QString::fromStdString(g.id));
    tempUnit_ = new QComboBox;
    tempUnit_->addItems({"Celsius (°C)", "Fahrenheit (°F)"});
    memUnit_ = new QComboBox;
    memUnit_->addItems({"Gigabytes (GB)", "Megabytes (MB)"});
    hf->addRow("GPU monitorada:", gpu_);
    hf->addRow("Temperatura:", tempUnit_);
    hf->addRow("Memória:", memUnit_);
    l->addWidget(hw);

    auto* app = new QGroupBox("Aplicativo");
    auto* apf = new QVBoxLayout(app);
    startMinimized_ = new QCheckBox("Iniciar minimizado na bandeja");
    autostart_ = new QCheckBox("Iniciar automaticamente com a sessão");
    apf->addWidget(startMinimized_);
    apf->addWidget(autostart_);
    l->addWidget(app);

    auto* help = new QGroupBox("Como usar nos jogos");
    auto* hl = new QVBoxLayout(help);
    auto* txt = new QLabel(
        "<b>Steam</b> (Propriedades → Opções de inicialização):<br><code>linosd-run %command%</code><br><br>"
        "<b>Lutris / Heroic / terminal</b>: prefixe o comando do jogo com <code>linosd-run</code>.<br>"
        "Ex.: <code>linosd-run vkcube</code><br><br>"
        "Atalho de teclado: configure no seu ambiente gráfico o comando "
        "<code>linosd --toggle</code> para mostrar/ocultar o overlay.<br>"
        "Arquivo de configuração: <code>" +
        Config::configPath().toHtmlEscaped() + "</code>");
    txt->setWordWrap(true);
    txt->setTextInteractionFlags(Qt::TextSelectableByMouse);
    hl->addWidget(txt);
    l->addWidget(help);
    l->addStretch();

    auto onEdit = [this] {
        if (!loading_) readGeneral();
    };
    for (QComboBox* c : {displayMode_, lowsMethod_, gpu_, tempUnit_, memUnit_})
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, onEdit);
    for (QSpinBox* s : {refreshMs_, sensorMs_, fpsWindow_, lowsWindow_, graphFrames_, fpsLimit_})
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, onEdit);
    connect(startMinimized_, &QCheckBox::toggled, this, onEdit);
    connect(autostart_, &QCheckBox::toggled, this, [this](bool on) {
        if (!loading_) setAutostart(on);
    });
    return w;
}

void ConfigWindow::setFpsLimit(int fps) { fpsLimit_->setValue(qBound(0, fps, 1000)); }

void ConfigWindow::readGeneral() {
    GeneralSettings& g = cfg_.general;
    g.displayMode = static_cast<DisplayMode>(displayMode_->currentIndex());
    g.refreshMs = refreshMs_->value();
    g.sensorMs = sensorMs_->value();
    g.fpsWindowMs = fpsWindow_->value();
    g.lowsWindowS = lowsWindow_->value();
    g.lowsAverage = lowsMethod_->currentIndex() == 1;
    g.graphFrames = graphFrames_->value();
    g.fpsLimit = fpsLimit_->value();
    g.gpuId = gpu_->currentData().toString();
    g.fahrenheit = tempUnit_->currentIndex() == 1;
    g.memoryInMB = memUnit_->currentIndex() == 1;
    g.startMinimized = startMinimized_->isChecked();
    changed();
}

static QString autostartPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/autostart/linosd.desktop";
}

bool ConfigWindow::autostartEnabled() const { return QFile::exists(autostartPath()); }

void ConfigWindow::setAutostart(bool on) {
    QString path = autostartPath();
    if (!on) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        statusBar()->showMessage("Não foi possível criar " + path, 4000);
        return;
    }
    f.write(QString("[Desktop Entry]\nType=Application\nName=LinOSD\nExec=\"%1\" --minimized\n"
                    "Icon=linosd\nX-GNOME-Autostart-enabled=true\n")
                .arg(QCoreApplication::applicationFilePath())
                .toUtf8());
}

// ===========================================================================
// Prévia
// ===========================================================================
QWidget* ConfigWindow::buildPreviewPanel() {
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    auto* hdr = new QHBoxLayout;
    auto* title = new QLabel("<b>Prévia</b>");
    scene_ = new QComboBox;
    scene_->addItems({"Cena escura", "Cena clara", "Xadrez"});
    hdr->addWidget(title);
    hdr->addStretch();
    hdr->addWidget(new QLabel("Fundo:"));
    hdr->addWidget(scene_);
    l->addLayout(hdr);
    preview_ = new PreviewWidget;
    l->addWidget(preview_, 1);
    overlayVisible_ = new QCheckBox("Overlay ativo na tela");
    overlayVisible_->setChecked(overlay_->userVisible());
    l->addWidget(overlayVisible_);
    auto* note = new QLabel("Sem jogo aberto, a prévia usa frames de demonstração; CPU, GPU e RAM são reais. "
                            "Todas as alterações são aplicadas e salvas automaticamente.");
    note->setWordWrap(true);
    note->setStyleSheet("color: gray;");
    l->addWidget(note);
    connect(scene_, qOverload<int>(&QComboBox::currentIndexChanged), preview_, &PreviewWidget::setScene);
    connect(overlayVisible_, &QCheckBox::toggled, this, [this](bool on) {
        if (!loading_) overlay_->setUserVisible(on);
    });
    return w;
}

void ConfigWindow::updatePreview() {
    MetricsSnapshot snap = metrics_->snapshot();
    if (!snap.frames.active) {
        snap.frames = demoFrameStats(cfg_.general.graphFrames);
        snap.demoFrames = true;
    }
    preview_->setImage(OsdRenderer::render(cfg_, snap, devicePixelRatioF()), cfg_.position.anchor);
}

void ConfigWindow::updateStatus() {
    const auto& f = metrics_->snapshot().frames;
    if (f.active) {
        status_->setText(QString("🎮 Jogo detectado: %1  ·  %2  ·  PID %3  ·  %4 FPS")
                             .arg(QString::fromStdString(f.process), QString::fromStdString(f.api))
                             .arg(f.pid)
                             .arg(linosd::has(f.fps) ? QString::number(f.fps, 'f', 0) : "--"));
    } else {
        status_->setText("Nenhum jogo detectado — inicie o jogo com “linosd-run <comando>” "
                         "(Steam: linosd-run %command%)");
    }
}

// ===========================================================================
void ConfigWindow::loadToUi() {
    loading_ = true;
    const OsdStyle& s = cfg_.style;
    labelFont_->setSpec(s.labelFont);
    valueFont_->setSpec(s.valueFont);
    unitFont_->setSpec(s.unitFont);
    scale_->setValue(qRound(s.scale * 100));
    labelColor_->setColor(s.labelColor);
    valueColor_->setColor(s.valueColor);
    unitColor_->setColor(s.unitColor);
    outlineGroup_->setChecked(s.outline);
    outlineColor_->setColor(s.outlineColor);
    outlineWidth_->setValue(s.outlineWidth);
    shadowGroup_->setChecked(s.shadow);
    shadowColor_->setColor(s.shadowColor);
    shadowX_->setValue(s.shadowX);
    shadowY_->setValue(s.shadowY);
    shadowBlur_->setValue(s.shadowBlur);
    bgGroup_->setChecked(s.background);
    bgColor_->setColor(s.backgroundColor);
    bgRadius_->setValue(s.cornerRadius);
    bgPadding_->setValue(s.padding);
    borderEnabled_->setChecked(s.border);
    borderColor_->setColor(s.borderColor);
    borderWidth_->setValue(s.borderWidth);
    layout_->setCurrentIndex(static_cast<int>(s.layout));
    lineSpacing_->setValue(s.lineSpacing);
    itemSpacing_->setValue(s.itemSpacing);
    alignLabels_->setChecked(s.alignLabels);
    fixedWidth_->setChecked(s.fixedValueWidth);
    opacity_->setValue(qRound(s.opacity * 100));
    opacityLabel_->setText(QString("%1%").arg(opacity_->value()));

    int si = screen_->findData(cfg_.position.screen);
    screen_->setCurrentIndex(si >= 0 ? si : 0);
    if (auto* b = anchorGroup_->button(static_cast<int>(cfg_.position.anchor))) b->setChecked(true);
    offsetX_->setValue(cfg_.position.offsetX);
    offsetY_->setValue(cfg_.position.offsetY);

    const GeneralSettings& g = cfg_.general;
    displayMode_->setCurrentIndex(static_cast<int>(g.displayMode));
    refreshMs_->setValue(g.refreshMs);
    sensorMs_->setValue(g.sensorMs);
    fpsWindow_->setValue(g.fpsWindowMs);
    lowsWindow_->setValue(g.lowsWindowS);
    lowsMethod_->setCurrentIndex(g.lowsAverage ? 1 : 0);
    graphFrames_->setValue(g.graphFrames);
    fpsLimit_->setValue(g.fpsLimit);
    int gi = gpu_->findData(g.gpuId);
    gpu_->setCurrentIndex(gi >= 0 ? gi : 0);
    tempUnit_->setCurrentIndex(g.fahrenheit ? 1 : 0);
    memUnit_->setCurrentIndex(g.memoryInMB ? 1 : 0);
    startMinimized_->setChecked(g.startMinimized);
    autostart_->setChecked(autostartEnabled());
    loading_ = false;
    refreshItemList(0);
}

void ConfigWindow::changed() {
    if (loading_) return;
    emit configChanged(cfg_);
    updatePreview();
    saveTimer_.start();
}

void ConfigWindow::closeEvent(QCloseEvent* e) {
    if (saveTimer_.isActive()) {
        saveTimer_.stop();
        cfg_.save();
    }
    if (moveMode_->isChecked()) moveMode_->setChecked(false);
    if (hideOnClose_) {
        hide();
        e->ignore();
        return;
    }
    QMainWindow::closeEvent(e);
    qApp->quit();
}
