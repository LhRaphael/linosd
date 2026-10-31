#!/usr/bin/env bash
# Gera os pacotes .deb do LinOSD para Debian, Ubuntu, Linux Mint, Pop!_OS e
# demais distribuições baseadas em apt.
#
#   ./scripts/package-deb.sh               compila (64 + 32-bit, se possível) e empacota
#   ./scripts/package-deb.sh --no-32bit    sem o pacote de hooks 32-bit
#   ./scripts/package-deb.sh --skip-build  usa o que já está em build/ e build32/
#
# Saída (em dist/):
#   linosd_<versão>-<rev>_amd64.deb          aplicativo + hooks 64-bit
#   linosd-hooks32_<versão>-<rev>_i386.deb   hooks para jogos 32-bit (pacote multiarch i386)
#
# Layout instalado (FHS / multiarch do Debian):
#   /usr/bin/linosd, /usr/bin/linosd-run
#   /usr/lib/x86_64-linux-gnu/linosd/liblinosd_{vk,gl}.so  hooks 64-bit
#   /usr/lib/i386-linux-gnu/linosd/liblinosd_{vk,gl}.so    hooks 32-bit (linosd-hooks32)
#   /usr/share/vulkan/implicit_layer.d/linosd_*.json       camada Vulkan
#   /usr/share/applications/linosd.desktop                 atalho no menu/painel
#   /usr/share/icons/hicolor/scalable/apps/linosd.svg      ícone
#   /usr/share/metainfo, /usr/share/man/man1, /usr/share/doc/linosd
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
want32=1
skip_build=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-32bit) want32=0 ;;
        --skip-build) skip_build=1 ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "opção desconhecida: $1" >&2; exit 1 ;;
    esac
    shift
done

for tool in dpkg-deb dpkg-architecture dpkg-shlibdeps; do
    command -v "$tool" >/dev/null || { echo "Falta '$tool' (sudo apt install dpkg-dev)." >&2; exit 1; }
done

VERSION="$(sed -n 's/^project(linosd VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
REVISION="${LINOSD_DEB_REVISION:-1}"
FULLVER="$VERSION-$REVISION"
DEB_ARCH="$(dpkg --print-architecture)"
MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"
case "$DEB_ARCH" in
    amd64) LAYER_ARCH=x86_64 ;;
    arm64) LAYER_ARCH=aarch64 ;;
    *) LAYER_ARCH="$DEB_ARCH" ;;
esac
[ "$DEB_ARCH" = amd64 ] || want32=0

# Mantenedor: DEBFULLNAME/DEBEMAIL (padrão Debian) ou a identidade do git.
MAINT_NAME="${DEBFULLNAME:-$(git config user.name 2>/dev/null || true)}"
MAINT_MAIL="${DEBEMAIL:-$(git config user.email 2>/dev/null || true)}"
MAINTAINER="${MAINT_NAME:-LinOSD} <${MAINT_MAIL:-linosd@localhost}>"

# ------------------------------------------------------------------ compilação
can_build32() {
    echo 'int main(){return 0;}' | g++ -m32 -x c++ - -o /dev/null 2>/dev/null
}
if [ $want32 = 1 ] && [ $skip_build = 0 ] && ! can_build32; then
    echo ">> AVISO: compilador 32-bit indisponível (sudo apt install g++-multilib)."
    echo "          Será gerado apenas o pacote 64-bit."
    want32=0
fi
if [ $skip_build = 0 ]; then
    if [ $want32 = 1 ]; then "$ROOT/scripts/build.sh" --with-32bit; else "$ROOT/scripts/build.sh"; fi
fi
[ -x "$ROOT/build/linosd" ] || { echo "build/linosd não encontrado." >&2; exit 1; }
if [ $want32 = 1 ] && [ ! -f "$ROOT/build32/liblinosd_gl.so" ]; then want32=0; fi

# ------------------------------------------------------------------ preparação
DIST="$ROOT/dist"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$DIST"
rm -f "$DIST"/linosd_*.deb "$DIST"/linosd-hooks32_*.deb
umask 022

PKG="$TMP/linosd"            # raiz do pacote principal
PKG32="$TMP/linosd-hooks32"  # raiz do pacote 32-bit
mkdir -p "$PKG/DEBIAN"
DOC=/usr/share/doc/linosd

put() {  # put <modo> <origem> <destino absoluto, incluindo a raiz>
    install -D -m "$1" "$2" "$3"
}

add_hooks() {  # add_hooks <raiz> <dir de build> <multiarch> <rótulo da camada>
    local root="$1" libdir="/usr/lib/$3/linosd" lib
    for lib in liblinosd_vk.so liblinosd_gl.so; do
        put 644 "$ROOT/$2/$lib" "$root$libdir/$lib"
        strip --strip-unneeded --remove-section=.comment "$root$libdir/$lib"
    done
    sed -e "s|@ARCH@|$4|g" -e "s|@LIBRARY_PATH@|$libdir/liblinosd_vk.so|g" \
        "$ROOT/data/VkLayer_linosd.json.in" > "$TMP/layer.json"
    put 644 "$TMP/layer.json" "$root/usr/share/vulkan/implicit_layer.d/linosd_$4.json"
}

add_lintian_override() {  # add_lintian_override <raiz> <pacote>
    # Tag que só faz sentido para pacotes enviados ao arquivo oficial do Debian.
    printf '%s: initial-upload-closes-no-bugs\n' "$2" > "$TMP/override"
    put 644 "$TMP/override" "$1/usr/share/lintian/overrides/$2"
}

# Versão mínima da glibc exigida pelas bibliotecas (símbolos versionados).
glibc_min() {
    objdump -T "$@" 2>/dev/null | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1
}

finish_pkg() {  # finish_pkg <raiz> <arquivo .deb>
    local root="$1" out="$DIST/$2"
    (cd "$root" && find usr -type f -print0 | sort -z | xargs -0 md5sum) > "$root/DEBIAN/md5sums"
    chmod 644 "$root/DEBIAN/control" "$root/DEBIAN/md5sums"
    find "$root" -type d -exec chmod 755 {} +
    # xz garante compatibilidade com o dpkg do Debian (o zstd, padrão do Ubuntu, não é lido por ele).
    dpkg-deb --root-owner-group -Zxz --build "$root" "$out" >/dev/null
    echo ">> Pacote gerado: $out"
}

# ============================================================ pacote principal
echo ">> Montando o pacote linosd $FULLVER ($DEB_ARCH)"

put 755 "$ROOT/build/linosd" "$PKG/usr/bin/linosd"
strip --strip-unneeded --remove-section=.comment --remove-section=.note "$PKG/usr/bin/linosd"
add_hooks "$PKG" build "$MULTIARCH" "$LAYER_ARCH"

# $LIB é expandido pelo ld.so em cada processo: lib/x86_64-linux-gnu ou lib/i386-linux-gnu.
sed -e "s|@GL_PRELOAD@|/usr/\$LIB/linosd/liblinosd_gl.so|g" -e "s|@LINOSD_BIN@|/usr/bin/linosd|g" \
    "$ROOT/data/linosd-run.in" > "$TMP/linosd-run"
put 755 "$TMP/linosd-run" "$PKG/usr/bin/linosd-run"

put 644 "$ROOT/data/linosd.desktop" "$PKG/usr/share/applications/linosd.desktop"
put 644 "$ROOT/data/linosd.svg" "$PKG/usr/share/icons/hicolor/scalable/apps/linosd.svg"
sed -e "s|@VERSION@|$VERSION|g" -e "s|@DATE@|$(date +%F)|g" "$ROOT/data/linosd.metainfo.xml" > "$TMP/metainfo.xml"
put 644 "$TMP/metainfo.xml" "$PKG/usr/share/metainfo/linosd.metainfo.xml"

for page in linosd.1 linosd-run.1; do
    sed "s|@VERSION@|$VERSION|g" "$ROOT/data/$page" | gzip -9n > "$TMP/$page.gz"
    put 644 "$TMP/$page.gz" "$PKG/usr/share/man/man1/$page.gz"
done

# Documentação (copyright, changelog e README conforme a política Debian)
gzip -9nc "$ROOT/README.md" > "$TMP/README.md.gz"
put 644 "$TMP/README.md.gz" "$PKG$DOC/README.md.gz"
{
    echo "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/"
    echo "Upstream-Name: linosd"
    echo
    echo "Files: *"
    echo "Copyright: $(date +%Y) ${MAINT_NAME:-LinOSD}"
    if [ -f "$ROOT/LICENSE" ]; then
        echo "License: see-below"
        sed -e 's/^$/./' -e 's/^/ /' "$ROOT/LICENSE"
    else
        echo "License: unspecified"
        echo " Nenhum arquivo LICENSE foi encontrado no projeto. Adicione um LICENSE na raiz"
        echo " do código-fonte e gere o pacote novamente."
    fi
} > "$TMP/copyright"
put 644 "$TMP/copyright" "$PKG$DOC/copyright"
changelog() {  # changelog <pacote>
    printf '%s (%s) unstable; urgency=medium\n\n  * Pacote gerado por scripts/package-deb.sh.\n\n -- %s  %s\n' \
        "$1" "$FULLVER" "$MAINTAINER" "$(LC_ALL=C date -R)" | gzip -9n
}
changelog linosd > "$TMP/changelog.gz"
put 644 "$TMP/changelog.gz" "$PKG$DOC/changelog.Debian.gz"
add_lintian_override "$PKG" linosd

# dpkg-shlibdeps calcula as dependências reais a partir das bibliotecas ligadas
# (ex.: libqt6widgets6 no Debian 12, libqt6widgets6t64 no Ubuntu 24.04).
mkdir -p "$TMP/shlibs/debian"
printf 'Source: linosd\n\nPackage: linosd\nArchitecture: any\n' > "$TMP/shlibs/debian/control"
DEPENDS="$(cd "$TMP/shlibs" && dpkg-shlibdeps -O --warnings=0 \
    -e"$PKG/usr/bin/linosd" -e"$PKG/usr/lib/$MULTIARCH/linosd/liblinosd_vk.so" \
    -e"$PKG/usr/lib/$MULTIARCH/linosd/liblinosd_gl.so" 2>/dev/null | sed -n 's/^shlibs:Depends=//p' || true)"
if [ -z "$DEPENDS" ]; then
    # Alternativa: pacotes donos das bibliotecas usadas (sem versão mínima).
    DEPENDS="$(ldd "$PKG/usr/bin/linosd" | awk '/=> \// {print $3}' | xargs -r readlink -f \
        | xargs -r dpkg -S 2>/dev/null | cut -d: -f1 | sort -u | paste -sd, | sed 's/,/, /g' || true)"
fi
DEPENDS="${DEPENDS:+$DEPENDS, }procps"

{
    echo "Package: linosd"
    echo "Version: $FULLVER"
    echo "Architecture: $DEB_ARCH"
    echo "Multi-Arch: foreign"
    echo "Maintainer: $MAINTAINER"
    echo "Installed-Size: $(du -sk --exclude=DEBIAN "$PKG" | cut -f1)"
    echo "Depends: $DEPENDS"
    echo "Recommends: libvulkan1, pci.ids | hwdata"
    echo "Section: utils"
    echo "Priority: optional"
    echo "Description: overlay de desempenho para jogos (FPS, 1% low, CPU, GPU, VRAM)"
    echo " O LinOSD exibe sobre os jogos FPS, frametime, 1% e 0.1% low, gráfico de"
    echo " frametime, uso, temperatura, clock e consumo de CPU e GPU, RAM e VRAM, no"
    echo " estilo do MSI Afterburner, com limitador de FPS e editor gráfico de cores,"
    echo " fontes, contorno, sombra, fundo e posição."
    echo " ."
    echo " Os frames são capturados por uma camada Vulkan implícita (inclusive Proton,"
    echo " DXVK e VKD3D) e por um hook OpenGL. Para usar, inicie o jogo com"
    echo " \"linosd-run <comando>\" ou, na Steam, \"linosd-run %command%\"."
    echo " ."
    echo " Para jogos 32-bit, instale também o pacote linosd-hooks32 (arquitetura i386)."
} > "$PKG/DEBIAN/control"
finish_pkg "$PKG" "linosd_${FULLVER}_${DEB_ARCH}.deb"

# ================================================ pacote 32-bit (multiarch i386)
if [ $want32 = 1 ]; then
    echo ">> Montando o pacote linosd-hooks32 $FULLVER (i386)"
    mkdir -p "$PKG32/DEBIAN"
    add_hooks "$PKG32" build32 i386-linux-gnu i686
    DOC32=/usr/share/doc/linosd-hooks32
    put 644 "$TMP/copyright" "$PKG32$DOC32/copyright"
    changelog linosd-hooks32 > "$TMP/changelog32.gz"
    put 644 "$TMP/changelog32.gz" "$PKG32$DOC32/changelog.Debian.gz"
    add_lintian_override "$PKG32" linosd-hooks32
    G32="$(glibc_min "$PKG32"/usr/lib/i386-linux-gnu/linosd/*.so)"
    {
        echo "Package: linosd-hooks32"
        echo "Source: linosd"
        echo "Version: $FULLVER"
        echo "Architecture: i386"
        echo "Multi-Arch: same"
        echo "Maintainer: $MAINTAINER"
        echo "Installed-Size: $(du -sk --exclude=DEBIAN "$PKG32" | cut -f1)"
        echo "Depends: libc6 (>= ${G32:-2.17}), linosd (= $FULLVER)"
        echo "Section: utils"
        echo "Priority: optional"
        echo "Description: overlay de desempenho para jogos - hooks para jogos 32-bit"
        echo " Camada Vulkan e hook OpenGL do LinOSD para processos 32-bit (jogos antigos,"
        echo " Wine/Proton 32-bit). Requer a arquitetura i386 habilitada no dpkg, o que já"
        echo " é o caso em sistemas com a Steam instalada."
    } > "$PKG32/DEBIAN/control"
    finish_pkg "$PKG32" "linosd-hooks32_${FULLVER}_i386.deb"
fi

echo "   Depends (linosd): $DEPENDS"
if command -v lintian >/dev/null; then
    echo ">> lintian:"
    lintian --no-tag-display-limit "$DIST"/linosd*.deb 2>/dev/null | sed 's/^/   /' || true
fi
echo
echo "Instalar:  sudo apt install $DIST/linosd_${FULLVER}_${DEB_ARCH}.deb"
if [ $want32 = 1 ]; then
    echo "  32-bit:  sudo dpkg --add-architecture i386 && sudo apt update   (se ainda não estiver ativa)"
    echo "           sudo apt install $DIST/linosd-hooks32_${FULLVER}_i386.deb"
fi
