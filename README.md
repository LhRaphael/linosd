# LinOSD — overlay de desempenho para jogos no Linux

Monitor de desempenho no estilo **MSI Afterburner / RivaTuner** para Linux. Exibe sobre o jogo:
FPS, frametime, **1% low**, **0.1% low**, mínimo/máximo, gráfico de frametime, uso/temperatura/clock/consumo
de CPU e GPU, RAM, swap, **VRAM**, ventoinha, bateria, relógio, tempo de jogo, nome do jogo, API gráfica e resolução.
Tem ainda limitador de FPS e uma interface gráfica para personalizar o OSD por completo.

Escrito em **C++17** (núcleo, hooks e interface Qt), com scripts Bash para dependências, compilação e instalação.

## Instalação (Debian, Ubuntu, Linux Mint, Pop!_OS e outras baseadas em apt)

```bash
./scripts/install-deps.sh     # compilador, CMake, Qt 6, headers do Vulkan, dpkg-dev, multilib
./scripts/install.sh          # compila, gera os pacotes .deb e os instala com o apt
```

O LinOSD é instalado como **pacote do sistema**, gerenciado pelo `apt`/`dpkg`. Ele aparece no menu de aplicativos e
no painel, e os binários ficam no `PATH` como qualquer outro programa:

| Caminho | Conteúdo |
|---|---|
| `/usr/bin/linosd`, `/usr/bin/linosd-run` | aplicativo e lançador de jogos |
| `/usr/lib/x86_64-linux-gnu/linosd/` | hooks 64-bit (camada Vulkan + OpenGL) |
| `/usr/lib/i386-linux-gnu/linosd/` | hooks 32-bit (pacote `linosd-hooks32:i386`) |
| `/usr/share/vulkan/implicit_layer.d/linosd_*.json` | registro da camada Vulkan |
| `/usr/share/applications/linosd.desktop` | atalho do menu (clique direito: mostrar/ocultar overlay, encerrar) |
| `/usr/share/icons/hicolor/scalable/apps/linosd.svg` | ícone |
| `/usr/share/metainfo/linosd.metainfo.xml` | metadados para lojas de aplicativos |
| `/usr/share/man/man1/linosd.1.gz`, `linosd-run.1.gz` | `man linosd`, `man linosd-run` |
| `/usr/share/doc/linosd/` | README, copyright e changelog |

Os hooks para **jogos 32-bit** ficam num pacote separado, `linosd-hooks32` (arquitetura i386, multiarch), como no
Debian. Ele é instalado automaticamente se a arquitetura i386 já estiver habilitada, o que é o caso em quem tem a
Steam. Para habilitá-la junto com a instalação: `./scripts/install.sh --enable-i386`.

**Remover:** `sudo apt remove linosd`, que remove também o `linosd-hooks32`. Também funciona
`./scripts/uninstall.sh`; com `--purge`, apaga ainda a configuração do usuário.

**Só gerar os pacotes** (para instalar em outra máquina): `./scripts/package-deb.sh` grava os arquivos em `dist/`. Para
instalar: `sudo apt install ./linosd_1.0.0-1_amd64.deb ./linosd-hooks32_1.0.0-1_i386.deb`. As dependências (ex.: as
bibliotecas Qt 6) são calculadas pelo `dpkg-shlibdeps` para a versão da distribuição em que o pacote foi gerado; gere-o
na mesma versão (ou mais antiga) do sistema de destino.

O nome e o e-mail do mantenedor no pacote vêm de `DEBFULLNAME`/`DEBEMAIL` ou, na falta deles, do `git config`.

### Outras opções

| Comando | Efeito |
|---|---|
| `./scripts/install.sh --no-32bit` | sem os hooks 32-bit |
| `./scripts/install.sh --user` | instalação manual só para o usuário, em `~/.local` (sem sudo, qualquer distribuição) |
| `./scripts/install.sh --prefix /usr/local` | instalação manual em outro prefixo (distribuições sem apt) |
| `./scripts/install.sh --enable-cpu-power` | libera a leitura do consumo da CPU (veja *Limitações*) |

## Uso

| Onde | Como |
|---|---|
| **Steam** | Propriedades do jogo → Opções de inicialização: `linosd-run %command%` |
| **Lutris** | Opções do sistema → *Command prefix*: `linosd-run` |
| **Heroic** | Configurações do jogo → *Wrapper*: `linosd-run` |
| **Terminal** | `linosd-run <comando do jogo>` (ex.: `linosd-run vkcube`) |

O `linosd-run` abre o overlay automaticamente, se ele ainda não estiver aberto. Para configurar, abra o
**LinOSD** pelo menu de aplicativos ou rode `linosd`.

### Linha de comando

```
linosd                  abre o overlay e a janela de configuração
linosd --minimized      só o overlay e o ícone na bandeja
linosd --toggle         mostra ou oculta o overlay (associe a um atalho de teclado do sistema)
linosd --fps-limit 60   altera o limitador de FPS em tempo real (0 = desligado)
linosd --quit           encerra
linosd --probe          diagnóstico: imprime no terminal tudo o que está sendo lido

linosd-run --fps-limit 60 <jogo>   inicia o jogo já com o FPS limitado
linosd-run --no-gl <jogo>          injeta só a camada Vulkan (sem o hook OpenGL)
```

## Personalização (interface gráfica)

Todas as alterações aparecem na hora, no overlay e na prévia, e são salvas automaticamente em
`~/.config/linosd/config.json`.

- **Itens / Métricas**: adicione, remova, duplique, reordene (arrastando) e ligue/desligue cada métrica.
  Para cada item é possível configurar:
  - rótulo e a exibição do rótulo e da unidade;
  - casas decimais;
  - se o item inicia uma nova linha ou entra na linha anterior (como `GPU 45% 65°C 1800MHz`);
  - cores próprias para rótulo, valor e unidade;
  - **alerta de cor** quando o valor fica acima ou abaixo de um limite (ex.: temperatura > 85 °C);
  - para o gráfico de frametime: tamanho, escala, espessura da linha, preenchimento e fundo;
  - para relógio e texto livre: o formato ou o texto exibido.
- **Aparência**: fonte, tamanho, negrito e itálico (separados para rótulos, valores e unidades), escala geral (para
  monitores 1440p/4K), cores globais, **contorno** (cor e espessura), **sombra** (cor, deslocamento X/Y e desfoque),
  **plano de fundo** (cor com transparência, cantos arredondados, margem e borda), layout vertical ou horizontal,
  espaçamentos, alinhamento dos rótulos, largura fixa dos valores (evita o OSD "tremer") e opacidade.
- **Posição**: monitor, 9 pontos de ancoragem, deslocamento X/Y e **arrastar o overlay com o mouse** direto na tela.
- **Geral**: quando mostrar o overlay (sempre ou só com jogo), taxas de atualização, janela do FPS médio e dos lows,
  método dos lows (percentil ou média dos piores frames), limitador de FPS, GPU monitorada, °C/°F, GB/MB,
  inicialização automática.
- **Presets**: Afterburner clássico, Completo, Minimalista, Horizontal (barra) e Neon. Perfis podem ser
  exportados e importados em JSON.

## Arquitetura

```
 ┌──────────────── processo do jogo ────────────────┐        ┌────────── linosd (Qt) ───────────┐
 │  vkQueuePresentKHR ─► liblinosd_vk.so (camada    │        │ FrameSource ─► FPS, frametime,   │
 │                       Vulkan implícita)          │ /dev/  │               1%/0.1% low, gráfico│
 │  glXSwapBuffers    ─► liblinosd_gl.so (LD_PRE-   │──shm──►│ CPU/RAM/bateria (/proc, /sys)    │
 │  eglSwapBuffers       LOAD, também via dlsym)    │◄─limite│ GPU: amdgpu sysfs · NVML · i915  │
 │      timestamp de cada frame + limitador de FPS  │ de FPS │ OsdRenderer ─► janela overlay    │
 └──────────────────────────────────────────────────┘        │ ConfigWindow (personalização)    │
                                                             └───────────────────────────────────┘
```

- **Hooks** (`src/hook/`): gravam o instante de cada *present* em um buffer circular lock-free em
  `/dev/shm/linosd.<pid>`, com layout idêntico em 32 e 64 bits. Exportam somente os símbolos necessários e usam a
  libstdc++ estática, para não interferir no jogo. Com Proton/DXVK/VKD3D, a captura é feita pela camada Vulkan.
- **Núcleo** (`src/core/`, sem Qt): leitura de sensores e cálculo das estatísticas de frames.
- **Aplicativo** (`src/app/`): renderização do OSD (texto vetorial com contorno, sombra com desfoque, fundo e
  gráficos), janela transparente *click-through* sempre no topo, interface de configuração e bandeja do sistema.

### Como os lows são calculados

- **FPS** e **frametime**: média dos frames na janela configurada (padrão: 500 ms).
- **1% / 0.1% low**: frametime nos percentis 99 e 99,9 dos frames da janela (padrão: 10 s), convertido em FPS, como
  no Afterburner/RTSS. A opção "média dos piores frames" segue o método do CapFrameX.
- **Mínimo / máximo**: extremos das leituras do FPS médio na mesma janela.

## Fontes das métricas

| Métrica | AMD | NVIDIA | Intel |
|---|---|---|---|
| Uso da GPU | `gpu_busy_percent` | NVML | — (exige perf/root) |
| VRAM | `mem_info_vram_*` | NVML | — (iGPU) |
| Temperatura / hotspot | hwmon (edge / junction) | NVML | hwmon (Arc) |
| Clock núcleo / memória | hwmon `freq1/2` ou `pp_dpm_*` | NVML | `gt_act_freq_mhz` / xe |
| Consumo | hwmon `power1_*` | NVML | hwmon `energy1` (Arc) |
| Ventoinha | hwmon `pwm1` / `fan1` | NVML | hwmon (Arc) |

- **CPU**: uso total e do núcleo mais ocupado (`/proc/stat`), temperatura (k10temp, zenpower, coretemp…), clock
  (`cpufreq`) e consumo (RAPL ou zenpower).
- **Memória**: `/proc/meminfo`. **Bateria**: `/sys/class/power_supply`.

A NVML (`libnvidia-ml.so.1`) é carregada em tempo de execução: não é preciso ter nada da NVIDIA para compilar.

## Limitações conhecidas

- O OSD é desenhado em uma **janela transparente sobre o jogo**, e não dentro do frame do jogo. Por isso:
  - ele precisa de um **compositor** (KDE, GNOME, Cinnamon, Xfce com composição etc.);
  - funciona com jogos em **janela** ou **tela cheia sem bordas**. Em tela cheia exclusiva, alguns compositores
    desativam a composição ("unredirect") e o overlay some. No KDE, desative "permitir que aplicativos bloqueiem a
    composição" ou use o modo sem bordas;
  - no **Wayland**, o overlay roda via XWayland. Jogos XWayland (a maioria dos jogos Proton/Wine e SDL) funcionam;
    jogos Wayland nativos em tela cheia podem ficar por cima do overlay;
  - o overlay não aparece em capturas de tela feitas *dentro* do jogo, nem no gamescope.
- Uso de GPU em placas **Intel** não está disponível sem privilégios de root.
- **Consumo da CPU (RAPL)**: kernels recentes restringem `energy_uj` ao root (mitigação do ataque PLATYPUS).
  `install.sh --enable-cpu-power` libera a leitura; avalie esse risco antes de usar. Com o módulo `zenpower`,
  funciona sem isso.
- **Steam Flatpak**: o sandbox tem o próprio `/dev/shm`, e por isso o FPS não chega ao overlay. Use o Steam nativo.
- **Anti-cheat**: camadas Vulkan e `LD_PRELOAD` são técnicas usadas também pelo MangoHud e costumam ser aceitas, mas
  alguns anti-cheats podem recusar bibliotecas injetadas. Para desativar só o hook GL, use `linosd-run --no-gl`.

## Estrutura

```
CMakeLists.txt
scripts/   install-deps.sh  build.sh  package-deb.sh  install.sh  uninstall.sh
data/      linosd-run.in  VkLayer_linosd.json.in  linosd.desktop  linosd.svg
           linosd.metainfo.xml  linosd.1  linosd-run.1
src/common/shm_protocol.h          protocolo de memória compartilhada
src/hook/  frame_recorder  vk_layer  gl_hook (+ .map de símbolos exportados)
src/core/  sysfs  system_monitor  gpu_monitor  frame_source
src/app/   main  config  metrics  osd_renderer  overlay_window  config_window  widgets
```

## Desinstalar

```bash
sudo apt remove linosd          # ou: ./scripts/uninstall.sh [--purge]
```

## Próximos passos possíveis

- Renderizar o OSD **dentro** do jogo (desenho na própria camada Vulkan), eliminando as limitações da janela externa.
- Gravação de benchmark em CSV (frametimes + sensores) e atalho global para iniciar/parar.
- Uso por processo da GPU (`fdinfo` do DRM) e suporte ao gamescope.
