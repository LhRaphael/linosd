#!/usr/bin/env bash
# Instala o LinOSD (rode ./scripts/build.sh antes).
#   ./scripts/install.sh                     instala em ~/.local (sem sudo)
#   ./scripts/install.sh --system            instala em /usr/local (sudo)
#   ./scripts/install.sh --prefix DIR        prefixo personalizado
#   ./scripts/install.sh --enable-cpu-power  libera a leitura do consumo da CPU (RAPL, requer sudo)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="$HOME/.local"
SUDO=""
cpu_power=0

while [ $# -gt 0 ]; do
    case "$1" in
        --system) PREFIX=/usr/local; [ "$(id -u)" -ne 0 ] && SUDO=sudo ;;
        --prefix) PREFIX="$2"; shift ;;
        --enable-cpu-power) cpu_power=1 ;;
        -h|--help) sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "opção desconhecida: $1" >&2; exit 1 ;;
    esac
    shift
done

[ -x "$ROOT/build/linosd" ] || { echo "Execute ./scripts/build.sh primeiro." >&2; exit 1; }

BIN="$PREFIX/bin"
HOOK_ROOT="$PREFIX/lib/linosd"
DATA="$PREFIX/share"
LAYER_DIR="$DATA/vulkan/implicit_layer.d"
MANIFEST="$DATA/linosd/installed-files.txt"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

files=()
put() {  # put <origem> <destino> <modo>
    $SUDO install -D -m "$3" "$1" "$2"
    files+=("$2")
}

echo ">> Instalando em $PREFIX"
put "$ROOT/build/linosd" "$BIN/linosd" 755

for arch_dir in build build32; do
    [ -f "$ROOT/$arch_dir/liblinosd_vk.so" ] || continue
    if [ "$arch_dir" = build ]; then arch=x86_64; else arch=i686; fi
    put "$ROOT/$arch_dir/liblinosd_vk.so" "$HOOK_ROOT/$arch/liblinosd_vk.so" 755
    put "$ROOT/$arch_dir/liblinosd_gl.so" "$HOOK_ROOT/$arch/liblinosd_gl.so" 755
    sed -e "s|@ARCH@|$arch|g" -e "s|@LIBRARY_PATH@|$HOOK_ROOT/$arch/liblinosd_vk.so|g" \
        "$ROOT/data/VkLayer_linosd.json.in" > "$TMP/linosd_$arch.json"
    put "$TMP/linosd_$arch.json" "$LAYER_DIR/linosd_$arch.json" 644
    echo "   hooks $arch instalados"
done
# O linosd-run usa LD_PRELOAD=".../$LIB/liblinosd_gl.so": o ld.so expande $LIB para o
# diretório de bibliotecas da arquitetura do processo, que varia por distribuição:
#   Debian/Ubuntu/Steam Runtime: lib/x86_64-linux-gnu | lib/i386-linux-gnu
#   Fedora/openSUSE:             lib64                | lib
#   Arch:                        lib                  | lib32
# Criamos links para todas as variantes apontando para x86_64/ e i686/.
host_lib="$(ldd /bin/sh 2>/dev/null | awk '/libc\.so/ {print $3}' | xargs -r dirname | sed -e 's|^/usr/||' -e 's|^/||')"
link() {  # link <alvo relativo> <caminho>
    $SUDO mkdir -p "$(dirname "$2")"
    $SUDO ln -sfn "$1" "$2"
    files+=("$2")
}
link ../x86_64 "$HOOK_ROOT/lib/x86_64-linux-gnu"
link ../i686 "$HOOK_ROOT/lib/i386-linux-gnu"
link x86_64 "$HOOK_ROOT/lib64"
link i686 "$HOOK_ROOT/lib32"
if [ "$host_lib" = "lib" ]; then lib_arch=x86_64; else lib_arch=i686; fi
link "../$lib_arch/liblinosd_gl.so" "$HOOK_ROOT/lib/liblinosd_gl.so"
link "../$lib_arch/liblinosd_vk.so" "$HOOK_ROOT/lib/liblinosd_vk.so"
echo "   \$LIB do sistema: ${host_lib:-?} (lib/ -> $lib_arch)"

sed -e "s|@HOOK_ROOT@|$HOOK_ROOT|g" -e "s|@LINOSD_BIN@|$BIN/linosd|g" "$ROOT/data/linosd-run.in" > "$TMP/linosd-run"
put "$TMP/linosd-run" "$BIN/linosd-run" 755
put "$ROOT/data/linosd.desktop" "$DATA/applications/linosd.desktop" 644
put "$ROOT/data/linosd.svg" "$DATA/icons/hicolor/scalable/apps/linosd.svg" 644

if [ $cpu_power = 1 ]; then
    # O kernel restringe energy_uj ao root (mitigação do ataque PLATYPUS).
    # Esta regra torna a leitura pública — avalie o risco no seu sistema.
    rule=/etc/tmpfiles.d/linosd-rapl.conf
    echo ">> Liberando leitura de RAPL ($rule)"
    printf 'z /sys/class/powercap/intel-rapl:0/energy_uj 0444 - - -\nz /sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj 0444 - - -\n' \
        | sudo tee "$rule" >/dev/null
    sudo systemd-tmpfiles --create "$rule" || true
    files+=("$rule")
fi

printf '%s\n' "${files[@]}" > "$TMP/manifest"
$SUDO install -D -m 644 "$TMP/manifest" "$MANIFEST"

command -v update-desktop-database >/dev/null && $SUDO update-desktop-database "$DATA/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null && $SUDO gtk-update-icon-cache -q -t "$DATA/icons/hicolor" 2>/dev/null || true

echo
echo ">> LinOSD instalado!"
case ":$PATH:" in
    *":$BIN:"*) ;;
    *) echo "   ATENÇÃO: $BIN não está no PATH. Adicione ao ~/.bashrc:  export PATH=\"$BIN:\$PATH\"" ;;
esac
[ -d "$HOOK_ROOT/i686" ] || echo "   Dica: para jogos 32-bit, rode ./scripts/build.sh --with-32bit e reinstale."
cat <<EOF

Uso:
  Abrir a configuração:     linosd
  Steam (opções de início): $BIN/linosd-run %command%
  Outros jogos:             linosd-run <comando do jogo>
  Mostrar/ocultar overlay:  linosd --toggle   (associe a um atalho de teclado)
EOF
