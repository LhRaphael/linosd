#!/usr/bin/env bash
# Instala as dependências de compilação e empacotamento do LinOSD.
#   ./scripts/install-deps.sh             padrão (no Debian/Ubuntu/Mint já inclui 32-bit e dpkg-dev)
#   ./scripts/install-deps.sh --32bit     outras distros: inclui também as de 32-bit
#   ./scripts/install-deps.sh --no-32bit  sem as dependências de 32-bit
set -euo pipefail

want32=""
case "${1:-}" in
    --32bit) want32=1 ;;
    --no-32bit) want32=0 ;;
esac

SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO="sudo"

if [ -r /etc/os-release ]; then . /etc/os-release; fi
echo ">> Distribuição: ${PRETTY_NAME:-desconhecida}"

if command -v apt-get >/dev/null; then
    # dpkg-dev: geração do pacote .deb (dpkg-deb, dpkg-shlibdeps)
    pkgs=(build-essential cmake pkg-config dpkg-dev qt6-base-dev libvulkan-dev)
    # multilib: hooks 32-bit (jogos 32-bit e Wine/Proton 32-bit) — padrão no amd64
    if [ "${want32:-1}" = 1 ] && [ "$(dpkg --print-architecture)" = amd64 ]; then
        pkgs+=(gcc-multilib g++-multilib)
    fi
    $SUDO apt-get update
    if ! $SUDO apt-get install -y "${pkgs[@]}"; then
        echo ">> Qt6 indisponível, tentando Qt5..."
        pkgs=("${pkgs[@]/qt6-base-dev/qtbase5-dev}")
        $SUDO apt-get install -y "${pkgs[@]}"
    fi
elif command -v dnf >/dev/null; then
    pkgs=(gcc-c++ cmake make pkgconf qt6-qtbase-devel vulkan-headers libstdc++-static hwdata)
    [ "$want32" = 1 ] && pkgs+=(glibc-devel.i686 libstdc++-devel.i686 libstdc++-static.i686)
    $SUDO dnf install -y "${pkgs[@]}"
elif command -v pacman >/dev/null; then
    pkgs=(base-devel cmake qt6-base vulkan-headers hwdata)
    [ "$want32" = 1 ] && pkgs+=(lib32-glibc lib32-gcc-libs)
    $SUDO pacman -S --needed --noconfirm "${pkgs[@]}"
elif command -v zypper >/dev/null; then
    pkgs=(gcc-c++ cmake make qt6-base-devel vulkan-headers hwdata)
    [ "$want32" = 1 ] && pkgs+=(gcc-32bit gcc-c++-32bit glibc-devel-32bit)
    $SUDO zypper install -y "${pkgs[@]}"
else
    echo "Gerenciador de pacotes não reconhecido. Instale manualmente:" >&2
    echo "  compilador C++17, CMake >= 3.16, Qt 6 (ou 5.15) Widgets, headers do Vulkan" >&2
    exit 1
fi

echo ">> Dependências instaladas. Próximo passo: ./scripts/install.sh"
