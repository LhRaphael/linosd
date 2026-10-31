// Leitura dos segmentos /dev/shm/linosd.<pid> e cálculo de FPS, frametime e lows.
#pragma once

#include <cstdint>
#include <deque>
#include <utility>
#include <string>
#include <vector>

#include "system_monitor.h"

namespace linosd {

struct ShmHeader;

struct FrameStats {
    bool active = false;       // há um jogo apresentando frames
    int pid = 0;
    std::string process;
    std::string api;
    double fps = kNaN;         // média na janela de amostragem
    double frametimeMs = kNaN; // média na janela de amostragem
    double low1 = kNaN;        // FPS 1% low (percentil 99 do frametime)
    double low01 = kNaN;       // FPS 0.1% low (percentil 99,9)
    double minFps = kNaN;      // menor leitura do FPS médio na janela dos lows
    double maxFps = kNaN;      // maior leitura do FPS médio na janela dos lows
    uint32_t width = 0, height = 0;
    uint32_t fpsLimit = 0;
    double sessionS = kNaN;    // tempo desde o primeiro frame do jogo
    std::vector<float> frametimes;  // ms, mais antigo -> mais recente (para o gráfico)
};

class FrameSource {
public:
    FrameSource();
    ~FrameSource();

    // fpsWindowMs: janela da média de FPS; lowsWindowS: janela dos lows;
    // graphFrames: quantos frametimes retornar para o gráfico.
    FrameStats sample(int fpsWindowMs, int lowsWindowS, int graphFrames);

    // Limitador de FPS (0 = sem limite). Só é gravado nos jogos quando o valor muda
    // (ou em jogos novos, se diferente de 0), preservando o LINOSD_FPS_LIMIT do jogo.
    void setFpsLimit(uint32_t fps) {
        if (fps != fpsLimit_) {
            fpsLimit_ = fps;
            limitChanged_ = true;
        }
    }

    // Lows calculados por percentil (padrão) ou pela média dos piores frames.
    void setLowsMethodAverage(bool avg) { lowsAverage_ = avg; }

private:
    struct Mapping {
        std::string path;
        ShmHeader* hdr = nullptr;
        size_t size = 0;
        bool writable = false;
    };
    std::vector<Mapping> maps_;
    uint64_t lastScanNs_ = 0;
    uint32_t fpsLimit_ = 0;
    bool limitChanged_ = false;
    bool lowsAverage_ = false;
    int lastPid_ = 0;
    std::deque<std::pair<uint64_t, double>> fpsHistory_;  // (instante, FPS médio)

    void rescan();
    void unmapAll();
};

}  // namespace linosd
