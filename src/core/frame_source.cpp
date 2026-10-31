#include "frame_source.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "../common/shm_protocol.h"
#include "sysfs.h"

namespace linosd {

namespace {
constexpr uint64_t kSecond = 1000000000ull;
constexpr uint64_t kActiveTimeout = 2 * kSecond;    // sem frames há 2 s = inativo
constexpr uint64_t kStaleTimeout = 120 * kSecond;   // remove segmentos órfãos
}  // namespace

FrameSource::FrameSource() { rescan(); }

FrameSource::~FrameSource() { unmapAll(); }

void FrameSource::unmapAll() {
    for (auto& m : maps_)
        if (m.hdr) munmap(m.hdr, m.size);
    maps_.clear();
}

void FrameSource::rescan() {
    std::vector<std::string> names;
    for (const auto& n : sysfs::listDir("/dev/shm"))
        if (sysfs::startsWith(n, kShmPrefix)) names.push_back("/dev/shm/" + n);

    // Remove mapeamentos de arquivos que sumiram.
    for (auto it = maps_.begin(); it != maps_.end();) {
        if (std::find(names.begin(), names.end(), it->path) == names.end()) {
            munmap(it->hdr, it->size);
            it = maps_.erase(it);
        } else {
            ++it;
        }
    }
    uint64_t now = monotonicNs();
    for (const auto& path : names) {
        auto known = std::find_if(maps_.begin(), maps_.end(), [&](const Mapping& m) { return m.path == path; });
        if (known != maps_.end()) {
            // Limpeza de segmentos de processos que morreram sem remover o arquivo.
            ShmHeader* h = known->hdr;
            uint64_t last = h->last_present_ns.load(std::memory_order_relaxed);
            if (now > last && now - last > kStaleTimeout && kill(h->pid, 0) != 0 && errno == ESRCH) {
                munmap(known->hdr, known->size);
                unlink(path.c_str());
                maps_.erase(known);
            }
            continue;
        }
        int fd = open(path.c_str(), O_RDWR);
        if (fd < 0) fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) continue;
        struct stat st;
        if (fstat(fd, &st) != 0 || static_cast<size_t>(st.st_size) < sizeof(ShmHeader)) {
            close(fd);
            continue;
        }
        int flags = fcntl(fd, F_GETFL);
        int prot = ((flags & O_ACCMODE) == O_RDWR) ? (PROT_READ | PROT_WRITE) : PROT_READ;
        void* mem = mmap(nullptr, sizeof(ShmHeader), prot, MAP_SHARED, fd, 0);
        close(fd);
        if (mem == MAP_FAILED) continue;
        auto* h = static_cast<ShmHeader*>(mem);
        if (h->magic != kShmMagic || h->version != kShmVersion) {
            munmap(mem, sizeof(ShmHeader));
            continue;
        }
        bool writable = (prot & PROT_WRITE) != 0;
        if (writable && fpsLimit_ != 0) h->fps_limit.store(fpsLimit_, std::memory_order_relaxed);
        maps_.push_back({path, h, sizeof(ShmHeader), writable});
    }
    lastScanNs_ = now;
}

FrameStats FrameSource::sample(int fpsWindowMs, int lowsWindowS, int graphFrames) {
    uint64_t now = monotonicNs();
    if (now - lastScanNs_ > kSecond) rescan();

    FrameStats st;
    // Escolhe o processo mais recentemente ativo.
    ShmHeader* best = nullptr;
    uint64_t bestLast = 0;
    for (auto& m : maps_) {
        uint64_t last = m.hdr->last_present_ns.load(std::memory_order_acquire);
        if (last > bestLast) {
            bestLast = last;
            best = m.hdr;
        }
    }
    if (!best || now < bestLast || now - bestLast > kActiveTimeout) return st;

    // Propaga uma mudança do limitador para todos os jogos.
    if (limitChanged_) {
        for (auto& m : maps_)
            if (m.writable) m.hdr->fps_limit.store(fpsLimit_, std::memory_order_relaxed);
        limitChanged_ = false;
    }

    st.active = true;
    st.pid = best->pid;
    st.process = std::string(best->process_name, strnlen(best->process_name, sizeof(best->process_name)));
    st.api = apiName(best->api);
    st.width = best->width.load(std::memory_order_relaxed);
    st.height = best->height.load(std::memory_order_relaxed);
    st.fpsLimit = best->fps_limit.load(std::memory_order_relaxed);
    uint64_t start = best->start_ns.load(std::memory_order_relaxed);
    if (start && now > start) st.sessionS = static_cast<double>(now - start) / 1e9;

    uint64_t count = best->frame_count.load(std::memory_order_acquire);
    if (count < 2) return st;
    uint64_t avail = std::min<uint64_t>(count, kRingSize - 64);  // margem contra sobrescrita

    // Copia os timestamps (mais recente primeiro) até cobrir a janela dos lows.
    uint64_t lowsWindow = static_cast<uint64_t>(std::max(1, lowsWindowS)) * kSecond;
    std::vector<uint64_t> ts;
    ts.reserve(static_cast<size_t>(avail));
    for (uint64_t i = 0; i < avail; ++i) {
        uint64_t t = best->frame_ns[(count - 1 - i) % kRingSize].load(std::memory_order_relaxed);
        if (t == 0) break;
        if (!ts.empty() && t > ts.back()) break;  // dado inconsistente (sobrescrito)
        ts.push_back(t);
        if (bestLast - t > lowsWindow && static_cast<int>(ts.size()) > graphFrames) break;
    }
    if (ts.size() < 2) return st;

    // Frametimes (ms), do mais recente para o mais antigo.
    std::vector<double> ft;
    ft.reserve(ts.size() - 1);
    for (size_t i = 0; i + 1 < ts.size(); ++i) ft.push_back(static_cast<double>(ts[i] - ts[i + 1]) / 1e6);

    // FPS médio: frames na janela de amostragem (mede até "agora", então cai a 0 se o jogo travar).
    uint64_t fpsWindow = static_cast<uint64_t>(std::max(100, fpsWindowMs)) * 1000000ull;
    size_t nWin = 0;
    uint64_t oldest = ts[0];
    for (size_t i = 0; i < ts.size(); ++i) {
        if (now - ts[i] > fpsWindow) break;
        oldest = ts[i];
        nWin = i;
    }
    if (nWin >= 1) {
        double span = static_cast<double>(ts[0] - oldest) / 1e9;
        // Se o jogo "travou" (último frame muito antigo), inclui o tempo até agora.
        double sinceLast = static_cast<double>(now - ts[0]) / 1e9;
        if (sinceLast > 2.0 * span / static_cast<double>(nWin)) span = static_cast<double>(now - oldest) / 1e9;
        st.fps = span > 0 ? static_cast<double>(nWin) / span : kNaN;
        st.frametimeMs = static_cast<double>(ts[0] - oldest) / 1e6 / static_cast<double>(nWin);
    } else {
        // Menos de 2 frames na janela (jogo abaixo de ~2 FPS): usa o último intervalo.
        st.frametimeMs = ft[0];
        st.fps = 1000.0 / ft[0];
    }

    // Lows sobre a janela configurada.
    std::vector<double> win;
    for (size_t i = 0; i < ft.size(); ++i) {
        if (bestLast - ts[i + 1] > lowsWindow) break;
        win.push_back(ft[i]);
    }
    if (win.size() >= 10) {
        std::vector<double> sorted = win;
        std::sort(sorted.begin(), sorted.end());  // crescente: piores no fim
        auto lowAt = [&](double fraction) {
            if (lowsAverage_) {
                // Média dos piores frames (estilo CapFrameX "1% low average")
                size_t n = std::max<size_t>(1, static_cast<size_t>(static_cast<double>(sorted.size()) * fraction));
                double sum = 0;
                for (size_t i = sorted.size() - n; i < sorted.size(); ++i) sum += sorted[i];
                return 1000.0 / (sum / static_cast<double>(n));
            }
            // Percentil (estilo Afterburner/RTSS)
            double pos = (1.0 - fraction) * static_cast<double>(sorted.size() - 1);
            size_t idx = static_cast<size_t>(pos);
            double frac = pos - static_cast<double>(idx);
            double v = sorted[idx];
            if (idx + 1 < sorted.size()) v += (sorted[idx + 1] - v) * frac;
            return 1000.0 / v;
        };
        st.low1 = lowAt(0.01);
        st.low01 = lowAt(0.001);
    }

    // Mínimo/máximo como no Afterburner: extremos das leituras do contador de FPS,
    // e não de frames isolados (que gerariam valores absurdos, ex.: 50000 FPS).
    if (st.pid != lastPid_) {
        fpsHistory_.clear();
        lastPid_ = st.pid;
    }
    if (has(st.fps)) fpsHistory_.emplace_back(now, st.fps);
    while (!fpsHistory_.empty() && now - fpsHistory_.front().first > lowsWindow) fpsHistory_.pop_front();
    if (!fpsHistory_.empty()) {
        double mn = fpsHistory_.front().second, mx = mn;
        for (const auto& e : fpsHistory_) {
            mn = std::min(mn, e.second);
            mx = std::max(mx, e.second);
        }
        st.minFps = mn;
        st.maxFps = mx;
    }

    // Frametimes para o gráfico (ordem cronológica).
    size_t g = std::min(ft.size(), static_cast<size_t>(std::max(2, graphFrames)));
    st.frametimes.resize(g);
    for (size_t i = 0; i < g; ++i) st.frametimes[g - 1 - i] = static_cast<float>(ft[i]);
    return st;
}

}  // namespace linosd
