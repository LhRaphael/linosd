// LinOSD - ponto de entrada.
//
//   linosd               abre o overlay + janela de configuração
//   linosd --minimized   inicia só com o overlay (bandeja)
//   linosd --toggle      mostra/oculta o overlay da instância em execução
//   linosd --show        abre a janela de configuração da instância em execução
//   linosd --fps-limit N altera o limitador de FPS (0 = desligado)
//   linosd --quit        encerra a instância em execução
//   linosd --probe       imprime as métricas no terminal (diagnóstico, sem interface)
#include <unistd.h>

#include <QCoreApplication>
#include <QThread>
#include <cstring>

#include <QApplication>
#include <QDir>
#include <QCommandLineParser>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QMenu>
#include <QSystemTrayIcon>
#include <cstdio>

#include "config.h"
#include "config_window.h"
#include "metrics.h"
#include "overlay_window.h"
#include "widgets.h"

#ifndef LINOSD_VERSION
#define LINOSD_VERSION "dev"
#endif

namespace {

QString socketName() { return QString("linosd-%1").arg(getuid()); }

bool sendToRunning(const QByteArray& cmd) {
    QLocalSocket s;
    s.connectToServer(socketName());
    if (!s.waitForConnected(300)) return false;
    s.write(cmd + "\n");
    s.flush();
    s.waitForBytesWritten(300);
    s.waitForReadyRead(300);
    return true;
}

bool hasArg(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], name) ||
            (!std::strncmp(argv[i], name, std::strlen(name)) && argv[i][std::strlen(name)] == '='))
            return true;
    return false;
}

QString fmt(double v, int dec = 0) { return linosd::has(v) ? QString::number(v, 'f', dec) : QStringLiteral("--"); }

// Modo diagnóstico: mostra no terminal o que o LinOSD está lendo.
int runProbe(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    MetricsCollector m;
    GeneralSettings g;
    m.applySettings(g);
    std::printf("CPU: %s\nGPU: %s\n", qPrintable(m.snapshot().cpuName), qPrintable(m.snapshot().gpuName));
    for (const auto& gi : m.gpus()) std::printf("  GPU detectada: %s %s (%s)\n", gi.vendor.c_str(), gi.name.c_str(), gi.id.c_str());
    std::printf("Ctrl+C para sair.\n\n");
    QObject::connect(&m, &MetricsCollector::updated, [&] {
        static int n = 0;
        if (++n % 10) return;  // ~1 linha por segundo
        const MetricsSnapshot& s = m.snapshot();
        const auto& f = s.frames;
        QString game = f.active ? QString("%1 [%2] FPS %3 FT %4ms 1% %5 0.1% %6")
                                      .arg(QString::fromStdString(f.process), QString::fromStdString(f.api), fmt(f.fps),
                                           fmt(f.frametimeMs, 2), fmt(f.low1), fmt(f.low01))
                                : QString("sem jogo");
        std::printf("%s | CPU %s%% %s°C %sMHz %sW | RAM %s/%s MiB | GPU %s%% %s°C %sMHz %sW VRAM %s MiB\n",
                    qPrintable(game), qPrintable(fmt(s.cpu.usage)), qPrintable(fmt(s.cpu.temp)),
                    qPrintable(fmt(s.cpu.freqMHz)), qPrintable(fmt(s.cpu.powerW)), qPrintable(fmt(s.mem.usedMiB)),
                    qPrintable(fmt(s.mem.totalMiB)), qPrintable(fmt(s.gpu.usage)), qPrintable(fmt(s.gpu.temp)),
                    qPrintable(fmt(s.gpu.coreMHz)), qPrintable(fmt(s.gpu.powerW)), qPrintable(fmt(s.gpu.vramUsedMiB)));
        std::fflush(stdout);
    });
    return app.exec();
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("LINOSD_DISABLE", "1");
    if (hasArg(argc, argv, "--probe")) return runProbe(argc, argv);

    // Comandos para a instância em execução não precisam de interface gráfica.
    const bool remote = hasArg(argc, argv, "--toggle") || hasArg(argc, argv, "--quit") ||
                        hasArg(argc, argv, "--fps-limit");
    if (remote) {
        QCoreApplication core(argc, argv);
        QByteArray cmd = hasArg(argc, argv, "--toggle") ? "toggle" : hasArg(argc, argv, "--quit") ? "quit" : "";
        if (cmd.isEmpty()) {
            for (int i = 1; i < argc; ++i) {
                if (!std::strcmp(argv[i], "--fps-limit") && i + 1 < argc) cmd = QByteArray("fps-limit ") + QByteArray(argv[i + 1]);
                else if (!std::strncmp(argv[i], "--fps-limit=", 12)) cmd = QByteArray("fps-limit ") + QByteArray(argv[i] + 12);
            }
        }
        if (sendToRunning(cmd)) return 0;
        std::fprintf(stderr, "linosd: nenhuma instância em execução.\n");
        return 1;
    }

    // O overlay precisa posicionar a janela e ficar sobre o jogo: no Wayland isso
    // só é possível via XWayland (os jogos via Proton/Wine também usam X11).
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    // Os processos do próprio overlay nunca devem ser monitorados.
    qunsetenv("LINOSD");

    QApplication app(argc, argv);
    QApplication::setApplicationName("linosd");
    QApplication::setApplicationDisplayName("LinOSD");
    QApplication::setApplicationVersion(LINOSD_VERSION);
    QApplication::setWindowIcon(makeAppIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription("LinOSD - overlay de desempenho para jogos no Linux");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption minimized("minimized", "Inicia minimizado (somente overlay e bandeja).");
    QCommandLineOption toggle("toggle", "Mostra/oculta o overlay da instância em execução.");
    QCommandLineOption show("show", "Abre a janela de configuração da instância em execução.");
    QCommandLineOption quit("quit", "Encerra a instância em execução.");
    QCommandLineOption fpsLimit("fps-limit", "Define o limitador de FPS (0 = desligado).", "fps");
    QCommandLineOption probe("probe", "Imprime as métricas no terminal (diagnóstico).");
    parser.addOptions({minimized, toggle, show, quit, fpsLimit, probe});
    parser.process(app);

    // Instância única: um lock evita duas instâncias ao clicar duas vezes no
    // atalho do menu; se já existe uma, apenas traz a janela para frente.
    QString runDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runDir.isEmpty()) runDir = QDir::tempPath();
    QLockFile lock(runDir + QString("/linosd-%1.lock").arg(getuid()));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(200)) {
        for (int i = 0; i < 20; ++i) {
            if (sendToRunning("show")) return 0;
            QThread::msleep(100);
        }
        std::fprintf(stderr, "linosd: outra instância está iniciando.\n");
        return 1;
    }

    Config cfg = Config::load();
    MetricsCollector metrics;
    metrics.applySettings(cfg.general);
    OverlayWindow overlay(&metrics);
    overlay.setConfig(cfg);
    ConfigWindow window(cfg, &metrics, &overlay);

    QObject::connect(&window, &ConfigWindow::configChanged, &app, [&](const Config& c) {
        overlay.setConfig(c);
        metrics.applySettings(c.general);
    });

    // Bandeja do sistema
    QSystemTrayIcon tray(makeAppIcon());
    QMenu trayMenu;
    QAction* actToggle = trayMenu.addAction("Ocultar overlay");
    QAction* actConfig = trayMenu.addAction("Configurações…");
    trayMenu.addSeparator();
    QAction* actQuit = trayMenu.addAction("Sair");
    auto showWindow = [&] {
        window.show();
        window.raise();
        window.activateWindow();
    };
    QObject::connect(actToggle, &QAction::triggered, [&] { overlay.setUserVisible(!overlay.userVisible()); });
    QObject::connect(&overlay, &OverlayWindow::userVisibleChanged,
                     [&](bool v) { actToggle->setText(v ? "Ocultar overlay" : "Mostrar overlay"); });
    QObject::connect(actConfig, &QAction::triggered, showWindow);
    QObject::connect(actQuit, &QAction::triggered, &app, &QApplication::quit);
    QObject::connect(&tray, &QSystemTrayIcon::activated, [&](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger) showWindow();
    });
    tray.setContextMenu(&trayMenu);
    tray.setToolTip("LinOSD");
    const bool hasTray = QSystemTrayIcon::isSystemTrayAvailable();
    if (hasTray) tray.show();
    app.setQuitOnLastWindowClosed(false);
    window.setHideOnClose(hasTray);

    // Instância única + comandos remotos
    QLocalServer server;
    QLocalServer::removeServer(socketName());
    server.listen(socketName());
    QObject::connect(&server, &QLocalServer::newConnection, [&] {
        while (QLocalSocket* s = server.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::readyRead, s, [&, s] {
                const QByteArray line = s->readAll().trimmed();
                if (line == "toggle") overlay.setUserVisible(!overlay.userVisible());
                else if (line == "show") showWindow();
                else if (line == "quit") app.quit();
                else if (line.startsWith("fps-limit ")) window.setFpsLimit(line.mid(10).toInt());
                s->write("ok\n");
                s->flush();
                s->disconnectFromServer();
            });
            QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
        }
    });

    if (!(parser.isSet(minimized) || cfg.general.startMinimized) || !hasTray) showWindow();
    return app.exec();
}
