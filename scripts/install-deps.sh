#!/usr/bin/env bash
# Instala as dependências de compilação do LinOSD.
#   ./scripts/install-deps.sh            dependências 64-bit
#   ./scripts/install-deps.sh --32bit    também as de 32-bit (hooks para jogos 32-bit)
set -euo pipefail

want32=0
[ "${1:-}" = "--32bit" ] && want32=1

SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO="sudo"

if [ -r /etc/os-release ]; then . /etc/os-release; fi
echo ">> Distribuição: ${PRETTY_NAME:-desconhecida}"

if command -v apt-get >/dev/null; then
    pkgs=(build-essential cmake pkg-config qt6-base-dev libvulkan-dev pciutils)
    [ $want32 = 1 ] && pkgs+=(gcc-multilib g++-multilib)
    $SUDO apt-get update
    if ! $SUDO apt-get install -y "${pkgs[@]}"; then
        echo ">> Qt6 indisponível, tentando Qt5..."
        pkgs=("${pkgs[@]/qt6-base-dev/qtbase5-dev}")
        $SUDO apt-get install -y "${pkgs[@]}"
    fi
elif command -v dnf >/dev/null; then
    pkgs=(gcc-c++ cmake make pkgconf qt6-qtbase-devel vulkan-headers libstdc++-static hwdata)
    [ $want32 = 1 ] && pkgs+=(glibc-devel.i686 libstdc++-devel.i686 libstdc++-static.i686)
    $SUDO dnf install -y "${pkgs[@]}"
elif command -v pacman >/dev/null; then
    pkgs=(base-devel cmake qt6-base vulkan-headers hwdata)
    [ $want32 = 1 ] && pkgs+=(lib32-glibc lib32-gcc-libs)
    $SUDO pacman -S --needed --noconfirm "${pkgs[@]}"
elif command -v zypper >/dev/null; then
    pkgs=(gcc-c++ cmake make qt6-base-devel vulkan-headers hwdata)
    [ $want32 = 1 ] && pkgs+=(gcc-32bit gcc-c++-32bit glibc-devel-32bit)
    $SUDO zypper install -y "${pkgs[@]}"
else
    echo "Gerenciador de pacotes não reconhecido. Instale manualmente:" >&2
    echo "  compilador C++17, CMake >= 3.16, Qt 6 (ou 5.15) Widgets, headers do Vulkan" >&2
    exit 1
fi

echo ">> Dependências instaladas. Próximo passo: ./scripts/build.sh"
