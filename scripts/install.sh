#!/usr/bin/env bash
# Instala o LinOSD.
#
#   ./scripts/install.sh                     Debian/Ubuntu/Mint (apt): gera o pacote .deb e o instala
#                                            com o apt, em /usr, como qualquer programa do sistema.
#   ./scripts/install.sh --no-32bit          idem, sem os hooks para jogos 32-bit
#   ./scripts/install.sh --enable-i386       habilita a arquitetura i386 no dpkg, se preciso,
#                                            para instalar os hooks 32-bit (Steam já a habilita)
#   ./scripts/install.sh --user              instala só para o usuário atual, em ~/.local (sem sudo)
#   ./scripts/install.sh --prefix DIR        instalação manual em DIR (ex.: /usr/local em outras distros)
#   ./scripts/install.sh --enable-cpu-power  libera a leitura do consumo da CPU (RAPL, requer sudo)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mode=""
PREFIX=""
cpu_power=0
enable_i386=0
pkg_args=()

while [ $# -gt 0 ]; do
    case "$1" in
        --user) mode=manual; PREFIX="$HOME/.local" ;;
        --system) mode=manual; PREFIX=/usr/local ;;   # compatibilidade com versões anteriores
        --prefix) mode=manual; PREFIX="$2"; shift ;;
        --no-32bit) pkg_args+=(--no-32bit) ;;
        --enable-i386) enable_i386=1 ;;
        --enable-cpu-power) cpu_power=1 ;;
        -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "opção desconhecida: $1" >&2; exit 1 ;;
    esac
    shift
done

SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO=sudo

if [ -z "$mode" ]; then
    if command -v apt-get >/dev/null && command -v dpkg-deb >/dev/null; then
        mode=deb
    elif command -v apt-get >/dev/null; then
        echo "Falta o dpkg-dev para gerar o pacote: rode ./scripts/install-deps.sh" >&2
        exit 1
    else
        mode=manual
        PREFIX=/usr/local
        echo ">> Sistema sem apt: instalação manual em $PREFIX"
    fi
fi

enable_cpu_power() {
    # O kernel restringe energy_uj ao root (mitigação do ataque PLATYPUS).
    # Esta regra torna a leitura pública: avalie o risco no seu sistema.
    local rule=/etc/tmpfiles.d/linosd-rapl.conf
    echo ">> Liberando a leitura de RAPL ($rule)"
    printf 'z /sys/class/powercap/intel-rapl:0/energy_uj 0444 - - -\nz /sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj 0444 - - -\n' \
        | sudo tee "$rule" >/dev/null
    sudo systemd-tmpfiles --create "$rule" 2>/dev/null || true
}

# Remove uma instalação manual anterior (evita que um binário antigo em
# ~/.local/bin ou /usr/local/bin tenha prioridade sobre o do pacote).
remove_manual_installs() {
    for p in "$HOME/.local" /usr/local; do
        if [ -f "$p/share/linosd/installed-files.txt" ]; then
            echo ">> Removendo instalação manual anterior em $p"
            "$ROOT/scripts/uninstall.sh" --prefix "$p" --manual-only >/dev/null
        fi
    done
}

print_usage() {
    cat <<EOF

Uso:
  Abrir:                    menu de aplicativos → LinOSD   (ou: linosd)
  Steam (opções de início): linosd-run %command%
  Outros jogos:             linosd-run <comando do jogo>
  Mostrar/ocultar overlay:  linosd --toggle   (associe a um atalho de teclado)
  Manual:                   man linosd   ·   man linosd-run
EOF
}

# =============================================================== pacote .deb
if [ "$mode" = deb ]; then
    "$ROOT/scripts/package-deb.sh" ${pkg_args[@]+"${pkg_args[@]}"}
    debs=("$(ls -t "$ROOT"/dist/linosd_*.deb | head -1)")

    # Hooks 32-bit: pacote i386 separado (multiarch), instalado se a arquitetura
    # i386 estiver habilitada — o que já acontece em sistemas com a Steam.
    deb32="$(ls -t "$ROOT"/dist/linosd-hooks32_*_i386.deb 2>/dev/null | head -1 || true)"
    if [ -n "$deb32" ]; then
        if [ $enable_i386 = 1 ] && ! dpkg --print-foreign-architectures | grep -qx i386; then
            echo ">> Habilitando a arquitetura i386 no dpkg"
            $SUDO dpkg --add-architecture i386
            $SUDO apt-get update
        fi
        if dpkg --print-foreign-architectures | grep -qx i386; then
            debs+=("$deb32")
        else
            echo ">> Arquitetura i386 não habilitada: os hooks 32-bit não serão instalados."
            echo "   Para jogos 32-bit, rode novamente com --enable-i386."
        fi
    fi

    remove_manual_installs

    # Cópia em diretório legível pelo usuário _apt (evita o aviso "unsandboxed").
    stage="$(mktemp -d)"
    chmod 755 "$stage"
    to_install=()
    for d in "${debs[@]}"; do
        install -m 644 "$d" "$stage/"
        to_install+=("$stage/$(basename "$d")")
    done
    echo ">> Instalando com o apt: $(printf '%s ' "${debs[@]##*/}")"
    $SUDO apt-get install -y --reinstall "${to_install[@]}"
    rm -rf "$stage"

    [ $cpu_power = 1 ] && enable_cpu_power

    echo
    echo ">> LinOSD instalado como pacote do sistema:"
    dpkg-query -W -f='   ${Package}:${Architecture} ${Version}\n' linosd linosd-hooks32 2>/dev/null || true
    echo "   Binários: $(command -v linosd), $(command -v linosd-run)"
    echo "   Remover:  sudo apt remove linosd   (ou ./scripts/uninstall.sh)"
    print_usage
    exit 0
fi

# ============================================================ instalação manual
[ -x "$ROOT/build/linosd" ] || "$ROOT/scripts/build.sh"
[ "$PREFIX" = "$HOME/.local" ] && SUDO=""
case "$PREFIX" in "$HOME"/*) SUDO="" ;; esac

BIN="$PREFIX/bin"
HOOK_ROOT="$PREFIX/lib/linosd"
DATA="$PREFIX/share"
MANIFEST="$DATA/linosd/installed-files.txt"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
VERSION="$(sed -n 's/^project(linosd VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"

files=()
put() {  # put <origem> <destino> <modo>
    $SUDO install -D -m "$3" "$1" "$2"
    files+=("$2")
}
link() {  # link <alvo relativo> <caminho>
    $SUDO mkdir -p "$(dirname "$2")"
    $SUDO ln -sfn "$1" "$2"
    files+=("$2")
}

echo ">> Instalando em $PREFIX"
put "$ROOT/build/linosd" "$BIN/linosd" 755
for arch_dir in build build32; do
    [ -f "$ROOT/$arch_dir/liblinosd_vk.so" ] || continue
    if [ "$arch_dir" = build ]; then arch=x86_64; else arch=i686; fi
    put "$ROOT/$arch_dir/liblinosd_vk.so" "$HOOK_ROOT/$arch/liblinosd_vk.so" 644
    put "$ROOT/$arch_dir/liblinosd_gl.so" "$HOOK_ROOT/$arch/liblinosd_gl.so" 644
    sed -e "s|@ARCH@|$arch|g" -e "s|@LIBRARY_PATH@|$HOOK_ROOT/$arch/liblinosd_vk.so|g" \
        "$ROOT/data/VkLayer_linosd.json.in" > "$TMP/linosd_$arch.json"
    put "$TMP/linosd_$arch.json" "$DATA/vulkan/implicit_layer.d/linosd_$arch.json" 644
    echo "   hooks $arch instalados"
done

# Fora do layout multiarch do Debian, $LIB varia por distribuição:
#   Debian/Ubuntu/Steam Runtime: lib/x86_64-linux-gnu | lib/i386-linux-gnu
#   Fedora/openSUSE:             lib64                | lib
#   Arch:                        lib                  | lib32
# Criamos links para todas as variantes apontando para x86_64/ e i686/.
host_lib="$(ldd /bin/sh 2>/dev/null | awk '/libc\.so/ {print $3}' | xargs -r dirname | sed -e 's|^/usr/||' -e 's|^/||')"
link ../x86_64 "$HOOK_ROOT/lib/x86_64-linux-gnu"
link ../i686 "$HOOK_ROOT/lib/i386-linux-gnu"
link x86_64 "$HOOK_ROOT/lib64"
link i686 "$HOOK_ROOT/lib32"
if [ "$host_lib" = "lib" ]; then lib_arch=x86_64; else lib_arch=i686; fi
link "../$lib_arch/liblinosd_gl.so" "$HOOK_ROOT/lib/liblinosd_gl.so"
link "../$lib_arch/liblinosd_vk.so" "$HOOK_ROOT/lib/liblinosd_vk.so"

sed -e "s|@GL_PRELOAD@|$HOOK_ROOT/\$LIB/liblinosd_gl.so|g" -e "s|@LINOSD_BIN@|$BIN/linosd|g" \
    "$ROOT/data/linosd-run.in" > "$TMP/linosd-run"
put "$TMP/linosd-run" "$BIN/linosd-run" 755
put "$ROOT/data/linosd.desktop" "$DATA/applications/linosd.desktop" 644
put "$ROOT/data/linosd.svg" "$DATA/icons/hicolor/scalable/apps/linosd.svg" 644
sed -e "s|@VERSION@|$VERSION|g" -e "s|@DATE@|$(date +%F)|g" "$ROOT/data/linosd.metainfo.xml" > "$TMP/metainfo.xml"
put "$TMP/metainfo.xml" "$DATA/metainfo/linosd.metainfo.xml" 644
for page in linosd.1 linosd-run.1; do
    sed "s|@VERSION@|$VERSION|g" "$ROOT/data/$page" | gzip -9n > "$TMP/$page.gz"
    put "$TMP/$page.gz" "$DATA/man/man1/$page.gz" 644
done

if [ $cpu_power = 1 ]; then
    enable_cpu_power
    files+=(/etc/tmpfiles.d/linosd-rapl.conf)
fi

printf '%s\n' "${files[@]}" > "$TMP/manifest"
$SUDO install -D -m 644 "$TMP/manifest" "$MANIFEST"

command -v update-desktop-database >/dev/null && $SUDO update-desktop-database "$DATA/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null && $SUDO gtk-update-icon-cache -q -t "$DATA/icons/hicolor" 2>/dev/null || true

echo
echo ">> LinOSD instalado em $PREFIX"
case ":$PATH:" in
    *":$BIN:"*) ;;
    *) echo "   ATENÇÃO: $BIN não está no PATH. Adicione ao ~/.bashrc:  export PATH=\"$BIN:\$PATH\"" ;;
esac
[ -d "$HOOK_ROOT/i686" ] || echo "   Dica: para jogos 32-bit, rode ./scripts/build.sh --with-32bit e reinstale."
print_usage
